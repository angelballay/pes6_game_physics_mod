#include "pch.h"
#include "GameplayConfig.h"
#include "ConfigStore.h"
#include "Logger.h"

#include <windows.h>
#include <string>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cctype>

namespace
{
    constexpr float DEFAULT_OVERALL_BALL_WEIGHT = 188.0f;
    constexpr float DEFAULT_POSSESSION_BALL_WEIGHT = 225.0f;
    constexpr float MIN_BALL_WEIGHT = 0.0f;
    constexpr float MAX_BALL_WEIGHT = 999.0f;

    const char* KEY_OVERALL_BALL_WEIGHT = "overall_ball_weight";
    const char* KEY_POSSESSION_BALL_WEIGHT = "ball_weight_possession";

    GameplayPhysicsConfig g_config = {
        DEFAULT_OVERALL_BALL_WEIGHT,
        DEFAULT_POSSESSION_BALL_WEIGHT
    };

    static std::string BuildConfigPath(HMODULE moduleHandle)
    {
        char path[MAX_PATH] = { 0 };

        if (moduleHandle && GetModuleFileNameA(moduleHandle, path, MAX_PATH) > 0)
        {
            char* lastSlash = strrchr(path, '\\');
            if (lastSlash)
            {
                *(lastSlash + 1) = '\0';
                strcat_s(path, MAX_PATH, "pes6_game_physics_mod.cfg");
                return std::string(path);
            }
        }

        return std::string("pes6_game_physics_mod.cfg");
    }

    static bool TryParseFloat(const std::string& text, float* outValue)
    {
        if (!outValue) return false;

        char* end = nullptr;
        errno = 0;
        float value = strtof(text.c_str(), &end);

        if (text.c_str() == end || errno == ERANGE)
            return false;

        while (end && *end)
        {
            if (!std::isspace((unsigned char)*end))
                return false;
            end++;
        }

        *outValue = value;
        return true;
    }

    static float ClampBallWeight(float value)
    {
        if (value < MIN_BALL_WEIGHT) return MIN_BALL_WEIGHT;
        if (value > MAX_BALL_WEIGHT) return MAX_BALL_WEIGHT;
        return value;
    }

    static std::string FormatFloatForCfg(float value)
    {
        float rounded = floorf(value + 0.5f);
        if (fabsf(value - rounded) < 0.001f)
        {
            char buffer[32];
            sprintf_s(buffer, sizeof(buffer), "%d", (int)rounded);
            return std::string(buffer);
        }

        std::ostringstream oss;
        oss << std::fixed << std::setprecision(3) << value;
        return oss.str();
    }

    static std::string GetPropertyWithAliases(const char* key, const char* const* aliases, int aliasCount)
    {
        std::string raw = ConfigStore::GetProperty(key, "");
        if (!raw.empty())
            return raw;

        for (int i = 0; i < aliasCount; ++i)
        {
            const char* alias = aliases[i];
            if (!alias || !alias[0])
                continue;

            raw = ConfigStore::GetProperty(alias, "");
            if (!raw.empty())
            {
                LogFormat("[CFG] Usando alias %s para %s. Valor='%s'", alias, key, raw.c_str());
                return raw;
            }
        }

        return std::string();
    }

    static float ReadValidatedBallWeight(const char* key, float defaultValue, const char* const* aliases = nullptr, int aliasCount = 0)
    {
        std::string raw = GetPropertyWithAliases(key, aliases, aliasCount);
        bool shouldRewrite = false;
        float value = defaultValue;

        if (raw.empty())
        {
            shouldRewrite = true;
        }
        else if (!TryParseFloat(raw, &value))
        {
            LogFormat("[CFG] Valor invalido para %s='%s'. Usando %.1f", key, raw.c_str(), defaultValue);
            value = defaultValue;
            shouldRewrite = true;
        }

        float clamped = ClampBallWeight(value);
        if (clamped != value)
        {
            LogFormat("[CFG] Valor fuera de rango para %s=%.3f. Clamp a %.3f", key, value, clamped);
            value = clamped;
            shouldRewrite = true;
        }

        // Siempre normalizamos la key canonica para evitar que el mod vuelva
        // a caer al default por errores de nombre o por editar otro alias.
        std::string canonicalRaw = ConfigStore::GetProperty(key, "");
        if (canonicalRaw.empty() || shouldRewrite)
        {
            ConfigStore::EditProperty(key, FormatFloatForCfg(value));
        }

        return value;
    }
}

bool LoadGameplayPhysicsConfig(HMODULE moduleHandle)
{
    std::string path = BuildConfigPath(moduleHandle);

    // Aunque no exista, Load deja seteada la ruta y luego EditProperty crea el archivo.
    ConfigStore::Load(path.c_str());
    LogFormat("[CFG] Ruta efectiva gameplay physics: %s", ConfigStore::GetConfigPath());

    g_config.overallBallWeight = ReadValidatedBallWeight(
        KEY_OVERALL_BALL_WEIGHT,
        DEFAULT_OVERALL_BALL_WEIGHT
    );

    const char* possessionAliases[] = {
        "possession_ball_weight",  // nombre alternativo natural
        "ball_weight_possesion",   // typo comun: una 's' menos en possession
        "ball_weight_posession"    // typo comun: posession
    };

    g_config.possessionBallWeight = ReadValidatedBallWeight(
        KEY_POSSESSION_BALL_WEIGHT,
        DEFAULT_POSSESSION_BALL_WEIGHT,
        possessionAliases,
        sizeof(possessionAliases) / sizeof(possessionAliases[0])
    );

    LogFormat(
        "[CFG] Pesos cargados: overall=%.3f possession=%.3f",
        g_config.overallBallWeight,
        g_config.possessionBallWeight
    );

    return true;
}

const GameplayPhysicsConfig& GetGameplayPhysicsConfig()
{
    return g_config;
}

float GetOverallBallWeight()
{
    return g_config.overallBallWeight;
}

float GetPossessionBallWeight()
{
    return g_config.possessionBallWeight;
}
