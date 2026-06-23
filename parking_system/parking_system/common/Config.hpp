/**
 * @file Config.hpp
 * @brief Simple KEY=VALUE config file parser for C++ components.
 *
 * Usage:
 *   Config cfg("config/server.cfg");
 *   int  port = cfg.getInt("PORT", 8080);
 *   auto path = cfg.get("LOG_FILE", "/tmp/parking_logs/server.log");
 */

#pragma once

#include <fstream>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <stdexcept>

class Config {
public:
    /**
     * @brief Load a config file.  Missing file is non-fatal (defaults apply).
     * @param path  Path to KEY=VALUE text file.
     */
    explicit Config(const std::string &path)
    {
        std::ifstream f(path);
        if (!f.is_open()) return;   /* silently use defaults */

        std::string line;
        while (std::getline(f, line)) {
            trim(line);
            if (line.empty() || line[0] == '#') continue;

            auto eq = line.find('=');
            if (eq == std::string::npos) continue;

            std::string key = line.substr(0, eq);
            std::string val = line.substr(eq + 1);
            trim(key);
            trim(val);
            map_[key] = val;
        }
    }

    /** @brief Return string value for key, or default_val if absent. */
    std::string get(const std::string &key,
                    const std::string &default_val = "") const
    {
        auto it = map_.find(key);
        return (it != map_.end()) ? it->second : default_val;
    }

    /** @brief Return int value for key, or default_val if absent/invalid. */
    int getInt(const std::string &key, int default_val = 0) const
    {
        auto it = map_.find(key);
        if (it == map_.end()) return default_val;
        try   { return std::stoi(it->second); }
        catch (...) { return default_val; }
    }

    /** @brief Return double value for key, or default_val if absent/invalid. */
    double getDouble(const std::string &key, double default_val = 0.0) const
    {
        auto it = map_.find(key);
        if (it == map_.end()) return default_val;
        try   { return std::stod(it->second); }
        catch (...) { return default_val; }
    }

private:
    std::unordered_map<std::string, std::string> map_;

    static void trim(std::string &s)
    {
        const char *ws = " \t\r\n";
        s.erase(0, s.find_first_not_of(ws));
        auto last = s.find_last_not_of(ws);
        if (last != std::string::npos) s.erase(last + 1);
        else s.clear();
    }
};
