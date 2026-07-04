#include "pch.h"
#include "ConfigStore.h"
#include "Logger.h"

#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <algorithm>
#include <cctype>

namespace
{
    std::map<std::string, std::string> g_properties;
    std::string g_configPath;

    static std::string Trim(const std::string& input)
    {
        size_t begin = 0;
        while (begin < input.size() && std::isspace((unsigned char)input[begin]))
            begin++;

        size_t end = input.size();
        while (end > begin && std::isspace((unsigned char)input[end - 1]))
            end--;

        return input.substr(begin, end - begin);
    }

    static bool IsCommentOrEmpty(const std::string& line)
    {
        std::string t = Trim(line);
        if (t.empty()) return true;
        if (t[0] == '#' || t[0] == ';') return true;
        if (t.size() >= 2 && t[0] == '/' && t[1] == '/') return true;
        return false;
    }
}

namespace ConfigStore
{
    bool Load(const char* path)
    {
        if (!path || !path[0])
        {
            WriteLog("[CFG] Ruta de config vacia.");
            return false;
        }

        g_configPath = path;
        g_properties.clear();

        std::ifstream file(g_configPath.c_str());
        if (!file.is_open())
        {
            LogFormat("[CFG] No existe config todavia: %s", g_configPath.c_str());
            return false;
        }

        std::string line;
        int lineNumber = 0;
        while (std::getline(file, line))
        {
            lineNumber++;

            if (IsCommentOrEmpty(line))
                continue;

            size_t eq = line.find('=');
            if (eq == std::string::npos)
            {
                LogFormat("[CFG] Linea ignorada %d: sin '='", lineNumber);
                continue;
            }

            std::string key = Trim(line.substr(0, eq));
            std::string value = Trim(line.substr(eq + 1));

            if (key.empty())
            {
                LogFormat("[CFG] Linea ignorada %d: key vacia", lineNumber);
                continue;
            }

            g_properties[key] = value;
        }

        LogFormat("[CFG] Config leida: %s", g_configPath.c_str());
        return true;
    }

    bool Save()
    {
        if (g_configPath.empty())
        {
            WriteLog("[CFG] No se puede guardar: ruta vacia.");
            return false;
        }

        std::ofstream file(g_configPath.c_str(), std::ios::trunc);
        if (!file.is_open())
        {
            LogFormat("[CFG] No se pudo guardar config: %s", g_configPath.c_str());
            return false;
        }

        file << "# PES 6 Game Physics Mod config\n";
        file << "# Formato: key=value\n";
        file << "# Valores de peso/magnetismo permitidos: 0..999\n";
        file << "# overall_ball_weight: valor base/restauracion cuando no hay conduccion\n";
        file << "# ball_weight_possession: valor usado cuando ball+84 == 0\n";
        file << "\n";

        for (const auto& kv : g_properties)
        {
            file << kv.first << "=" << kv.second << "\n";
        }

        LogFormat("[CFG] Config guardada: %s", g_configPath.c_str());
        return true;
    }

    std::string GetProperty(const std::string& key, const std::string& defaultValue)
    {
        auto it = g_properties.find(key);
        if (it == g_properties.end())
            return defaultValue;

        return it->second;
    }

    bool EditProperty(const std::string& key, const std::string& value)
    {
        if (key.empty())
            return false;

        g_properties[key] = value;
        return Save();
    }

    const char* GetConfigPath()
    {
        return g_configPath.c_str();
    }
}
