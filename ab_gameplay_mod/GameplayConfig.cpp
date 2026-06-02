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
    constexpr float DEFAULT_BALL_WEIGHT_STATE_1 = 198.0f;
    constexpr float DEFAULT_NORMAL_DRIBBLE_BALL_WEIGHT = 188.0f;
    constexpr float DEFAULT_R1_BALL_WEIGHT = 305.0f;
    constexpr float DEFAULT_R2_BALL_WEIGHT = 230.0f;
    constexpr float DEFAULT_R1_R2_BALL_WEIGHT = 188.0f;

    constexpr uint32_t DEFAULT_PROTECTED_ACTION_LATCH_MS = 0;
    constexpr uint32_t DEFAULT_R2_CHARGE_WINDOW_MS = 600;
    constexpr uint32_t DEFAULT_R2_CHARGE_START_DIST_MAX = 400;
    constexpr uint32_t DEFAULT_R2_CHARGE_KEEP_DIST_MAX = 700;

    constexpr uint32_t DEFAULT_DEBUG_TOUCHDBG = 0;
    constexpr uint32_t DEFAULT_DEBUG_BWDEC = 0;
    constexpr uint32_t DEFAULT_DEBUG_CHARGEDBG = 0;
    constexpr uint32_t DEFAULT_DEBUG_ACTOR_DEBUG_LOGGER = 0;

    constexpr float MIN_BALL_WEIGHT = 0.0f;
    constexpr float MAX_BALL_WEIGHT = 999.0f;

    constexpr uint32_t MIN_MS = 0;
    constexpr uint32_t MAX_MS = 5000;

    const char* KEY_OVERALL_BALL_WEIGHT = "overall_ball_weight";
    const char* KEY_BALL_WEIGHT_STATE_1 = "ball_weight_state_1";
    const char* KEY_NORMAL_DRIBBLE_BALL_WEIGHT = "ball_weight_normal_dribble";
    const char* KEY_R1_BALL_WEIGHT = "ball_weight_r1";
    const char* KEY_R2_BALL_WEIGHT = "ball_weight_r2";
    const char* KEY_R1_R2_BALL_WEIGHT = "ball_weight_r1_r2";
    const char* KEY_PROTECTED_ACTION_LATCH_MS = "protected_action_latch_ms";

    const char* KEY_R2_CHARGE_WINDOW_MS = "r2_charge_window_ms";
    const char* KEY_R2_CHARGE_START_DIST_MAX = "r2_charge_start_dist_max";
    const char* KEY_R2_CHARGE_KEEP_DIST_MAX = "r2_charge_keep_dist_max";

    const char* KEY_DEBUG_TOUCHDBG = "debug_touchdbg";
    const char* KEY_DEBUG_BWDEC = "debug_bwdec";
    const char* KEY_DEBUG_CHARGEDBG = "debug_chargedbg";
    const char* KEY_DEBUG_ACTOR_DEBUG_LOGGER = "debug_actor_debug_logger";

    GameplayPhysicsConfig g_config = {
        DEFAULT_OVERALL_BALL_WEIGHT,
        DEFAULT_BALL_WEIGHT_STATE_1,
        DEFAULT_NORMAL_DRIBBLE_BALL_WEIGHT,
        DEFAULT_R1_BALL_WEIGHT,
        DEFAULT_R2_BALL_WEIGHT,
        DEFAULT_R1_R2_BALL_WEIGHT,
        DEFAULT_PROTECTED_ACTION_LATCH_MS,
        DEFAULT_R2_CHARGE_WINDOW_MS,
        DEFAULT_R2_CHARGE_START_DIST_MAX,
        DEFAULT_R2_CHARGE_KEEP_DIST_MAX,
        DEFAULT_R1_BALL_WEIGHT,
        DEFAULT_DEBUG_TOUCHDBG,
        DEFAULT_DEBUG_BWDEC,
        DEFAULT_DEBUG_CHARGEDBG,
        DEFAULT_DEBUG_ACTOR_DEBUG_LOGGER
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
        if (!outValue)
            return false;

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

    static bool TryParseUInt(const std::string& text, uint32_t* outValue)
    {
        if (!outValue)
            return false;

        char* end = nullptr;
        errno = 0;

        unsigned long value = strtoul(text.c_str(), &end, 10);

        if (text.c_str() == end || errno == ERANGE)
            return false;

        while (end && *end)
        {
            if (!std::isspace((unsigned char)*end))
                return false;

            end++;
        }

        *outValue = static_cast<uint32_t>(value);
        return true;
    }

    static float ClampBallWeight(float value)
    {
        if (value < MIN_BALL_WEIGHT)
            return MIN_BALL_WEIGHT;

        if (value > MAX_BALL_WEIGHT)
            return MAX_BALL_WEIGHT;

        return value;
    }

    static uint32_t ClampMs(uint32_t value)
    {
        if (value < MIN_MS)
            return MIN_MS;

        if (value > MAX_MS)
            return MAX_MS;

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

    static std::string FormatUIntForCfg(uint32_t value)
    {
        char buffer[32];
        sprintf_s(buffer, sizeof(buffer), "%u", static_cast<unsigned int>(value));
        return std::string(buffer);
    }

    static std::string GetPropertyWithAliases(
        const char* key,
        const char* const* aliases,
        int aliasCount)
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
                LogFormat("[CFG] Usando alias %s para %s. Valor='%s'",
                    alias, key, raw.c_str());
                return raw;
            }
        }

        return std::string();
    }

    static float ReadValidatedBallWeight(
        const char* key,
        float defaultValue,
        const char* const* aliases = nullptr,
        int aliasCount = 0)
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
            LogFormat("[CFG] Valor invalido para %s='%s'. Usando %.1f",
                key, raw.c_str(), defaultValue);
            value = defaultValue;
            shouldRewrite = true;
        }

        float clamped = ClampBallWeight(value);

        if (clamped != value)
        {
            LogFormat("[CFG] Valor fuera de rango para %s=%.3f. Clamp a %.3f",
                key, value, clamped);
            value = clamped;
            shouldRewrite = true;
        }

        std::string canonicalRaw = ConfigStore::GetProperty(key, "");

        if (canonicalRaw.empty() || shouldRewrite)
            ConfigStore::EditProperty(key, FormatFloatForCfg(value));

        return value;
    }

    static uint32_t ReadValidatedMs(const char* key, uint32_t defaultValue)
    {
        std::string raw = ConfigStore::GetProperty(key, "");

        bool shouldRewrite = false;
        uint32_t value = defaultValue;

        if (raw.empty())
        {
            shouldRewrite = true;
        }
        else if (!TryParseUInt(raw, &value))
        {
            LogFormat("[CFG] Valor invalido para %s='%s'. Usando %u",
                key, raw.c_str(), static_cast<unsigned int>(defaultValue));
            value = defaultValue;
            shouldRewrite = true;
        }

        uint32_t clamped = ClampMs(value);

        if (clamped != value)
        {
            LogFormat("[CFG] Valor fuera de rango para %s=%u. Clamp a %u",
                key,
                static_cast<unsigned int>(value),
                static_cast<unsigned int>(clamped));
            value = clamped;
            shouldRewrite = true;
        }

        std::string canonicalRaw = ConfigStore::GetProperty(key, "");

        if (canonicalRaw.empty() || shouldRewrite)
            ConfigStore::EditProperty(key, FormatUIntForCfg(value));

        return value;
    }

    static uint32_t ReadValidatedFlag(const char* key, uint32_t defaultValue)
    {
        std::string raw = ConfigStore::GetProperty(key, "");
        bool shouldRewrite = false;
        uint32_t value = defaultValue ? 1u : 0u;

        if (raw.empty())
        {
            shouldRewrite = true;
        }
        else if (!TryParseUInt(raw, &value))
        {
            LogFormat(
                "[CFG] Valor invalido para %s='%s'. Usando %u",
                key,
                raw.c_str(),
                static_cast<unsigned int>(defaultValue ? 1u : 0u)
            );
            value = defaultValue ? 1u : 0u;
            shouldRewrite = true;
        }

        value = value ? 1u : 0u;

        std::string canonicalRaw = ConfigStore::GetProperty(key, "");
        if (canonicalRaw.empty() || shouldRewrite || canonicalRaw != FormatUIntForCfg(value))
            ConfigStore::EditProperty(key, FormatUIntForCfg(value));

        return value;
    }
}

bool LoadGameplayPhysicsConfig(HMODULE moduleHandle)
{
    std::string path = BuildConfigPath(moduleHandle);

    ConfigStore::Load(path.c_str());

    LogFormat("[CFG] Ruta efectiva gameplay physics: %s", ConfigStore::GetConfigPath());

    g_config.overallBallWeight =
        ReadValidatedBallWeight(KEY_OVERALL_BALL_WEIGHT, DEFAULT_OVERALL_BALL_WEIGHT);

    g_config.ballWeightState1 =
        ReadValidatedBallWeight(KEY_BALL_WEIGHT_STATE_1, DEFAULT_BALL_WEIGHT_STATE_1);

    g_config.normalDribbleBallWeight =
        ReadValidatedBallWeight(KEY_NORMAL_DRIBBLE_BALL_WEIGHT, DEFAULT_NORMAL_DRIBBLE_BALL_WEIGHT);

    const char* r1Aliases[] = {
        "ball_weight_possession",
        "ball_weight_possesion",
        "ball_weight_posession"
    };

    g_config.r1BallWeight =
        ReadValidatedBallWeight(
            KEY_R1_BALL_WEIGHT,
            DEFAULT_R1_BALL_WEIGHT,
            r1Aliases,
            sizeof(r1Aliases) / sizeof(r1Aliases[0]));

    g_config.r2BallWeight =
        ReadValidatedBallWeight(KEY_R2_BALL_WEIGHT, DEFAULT_R2_BALL_WEIGHT);

    g_config.r1R2BallWeight =
        ReadValidatedBallWeight(KEY_R1_R2_BALL_WEIGHT, DEFAULT_R1_R2_BALL_WEIGHT);

    g_config.protectedActionLatchMs =
        ReadValidatedMs(KEY_PROTECTED_ACTION_LATCH_MS, DEFAULT_PROTECTED_ACTION_LATCH_MS);

    g_config.r2ChargeWindowMs =
        ReadValidatedMs(KEY_R2_CHARGE_WINDOW_MS, DEFAULT_R2_CHARGE_WINDOW_MS);

    g_config.r2ChargeStartDistMax =
        ReadValidatedMs(KEY_R2_CHARGE_START_DIST_MAX, DEFAULT_R2_CHARGE_START_DIST_MAX);

    g_config.r2ChargeKeepDistMax =
        ReadValidatedMs(KEY_R2_CHARGE_KEEP_DIST_MAX, DEFAULT_R2_CHARGE_KEEP_DIST_MAX);

    g_config.debugTouchDbg =
        ReadValidatedFlag(KEY_DEBUG_TOUCHDBG, DEFAULT_DEBUG_TOUCHDBG);

    g_config.debugBwDec =
        ReadValidatedFlag(KEY_DEBUG_BWDEC, DEFAULT_DEBUG_BWDEC);

    g_config.debugChargeDbg =
        ReadValidatedFlag(KEY_DEBUG_CHARGEDBG, DEFAULT_DEBUG_CHARGEDBG);

    g_config.debugActorDebugLogger =
        ReadValidatedFlag(KEY_DEBUG_ACTOR_DEBUG_LOGGER, DEFAULT_DEBUG_ACTOR_DEBUG_LOGGER);

    g_config.possessionBallWeight = g_config.r1BallWeight;

    LogFormat(
        "[CFG] Pesos cargados: overall=%.3f state1=%.3f normal=%.3f "
        "r1=%.3f r2=%.3f r1r2=%.3f latchMs=%u "
        "r2ChargeMs=%u r2StartDist=%u r2KeepDist=%u",
        g_config.overallBallWeight,
        g_config.ballWeightState1,
        g_config.normalDribbleBallWeight,
        g_config.r1BallWeight,
        g_config.r2BallWeight,
        g_config.r1R2BallWeight,
        static_cast<unsigned int>(g_config.protectedActionLatchMs),
        static_cast<unsigned int>(g_config.r2ChargeWindowMs),
        static_cast<unsigned int>(g_config.r2ChargeStartDistMax),
        static_cast<unsigned int>(g_config.r2ChargeKeepDistMax),
        static_cast<unsigned int>(g_config.debugTouchDbg),
        static_cast<unsigned int>(g_config.debugBwDec),
        static_cast<unsigned int>(g_config.debugChargeDbg),
        static_cast<unsigned int>(g_config.debugActorDebugLogger));

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

float GetBallWeightState1()
{
    return g_config.ballWeightState1;
}

float GetBallWeightNormalDribble()
{
    return g_config.normalDribbleBallWeight;
}

float GetBallWeightR1()
{
    return g_config.r1BallWeight;
}

float GetBallWeightR2()
{
    return g_config.r2BallWeight;
}

float GetBallWeightR1R2()
{
    return g_config.r1R2BallWeight;
}

uint32_t GetProtectedActionLatchMs()
{
    return g_config.protectedActionLatchMs;
}

uint32_t GetR2ChargeWindowMs()
{
    return g_config.r2ChargeWindowMs;
}

uint32_t GetR2ChargeStartDistMax()
{
    return g_config.r2ChargeStartDistMax;
}

uint32_t GetR2ChargeKeepDistMax()
{
    return g_config.r2ChargeKeepDistMax;
}

float GetPossessionBallWeight()
{
    return g_config.possessionBallWeight;
}

uint32_t GetDebugTouchDbg()
{
    return g_config.debugTouchDbg;
}

uint32_t GetDebugBwDec()
{
    return g_config.debugBwDec;
}

uint32_t GetDebugChargeDbg()
{
    return g_config.debugChargeDbg;
}

uint32_t GetDebugActorDebugLogger()
{
    return g_config.debugActorDebugLogger;
}
