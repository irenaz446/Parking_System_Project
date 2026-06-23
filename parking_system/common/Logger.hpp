/**
 * @file Logger.hpp
 * @brief Thread-safe, timestamped logger for C++ components (server, db).
 *
 * Usage:
 *   Logger::init("/tmp/parking_logs/server.log");
 *   Logger::info("Server started on port {}", 8080);
 *   Logger::error("bind() failed: {}", strerror(errno));
 *
 * All public methods are static so any translation unit can call them
 * after a single Logger::init() call.  A std::mutex serialises writes.
 */

#pragma once

#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <ctime>
#include <cstdarg>

class Logger {
public:
    enum class Level { INFO, WARN, ERROR };

    /** @brief Open (append) the log file.  Call once at startup. */
    static void init(const std::string &path)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (file_.is_open()) file_.close();
        file_.open(path, std::ios::app);
        if (!file_.is_open())
            std::cerr << "[Logger] Cannot open: " << path << "\n";
    }

    static void info (const std::string &msg) { write(Level::INFO,  msg); }
    static void warn (const std::string &msg) { write(Level::WARN,  msg); }
    static void error(const std::string &msg) { write(Level::ERROR, msg); }

    /** @brief Convenience: build a message with a single appended value. */
    template<typename T>
    static void info (const std::string &msg, T val) { write(Level::INFO,  fmt(msg, val)); }
    template<typename T>
    static void warn (const std::string &msg, T val) { write(Level::WARN,  fmt(msg, val)); }
    template<typename T>
    static void error(const std::string &msg, T val) { write(Level::ERROR, fmt(msg, val)); }

    static void close()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (file_.is_open()) file_.close();
    }

private:
    static std::ofstream file_;
    static std::mutex    mutex_;

    static const char *levelStr(Level l)
    {
        switch (l) {
            case Level::INFO:  return "INFO ";
            case Level::WARN:  return "WARN ";
            case Level::ERROR: return "ERROR";
        }
        return "?????";
    }

    static std::string timestamp()
    {
        char buf[32];
        time_t now = time(nullptr);
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
        return buf;
    }

    static void write(Level l, const std::string &msg)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string line = "[" + timestamp() + "] [" + levelStr(l) + "] " + msg + "\n";
        std::ostream &out = file_.is_open() ? static_cast<std::ostream&>(file_)
                                            : static_cast<std::ostream&>(std::cerr);
        out << line;
        out.flush();
    }

    /** @brief Simple one-placeholder formatter (replaces first {} with val). */
    template<typename T>
    static std::string fmt(const std::string &tmpl, T val)
    {
        std::ostringstream oss;
        auto pos = tmpl.find("{}");
        if (pos != std::string::npos)
            oss << tmpl.substr(0, pos) << val << tmpl.substr(pos + 2);
        else
            oss << tmpl << " " << val;
        return oss.str();
    }
};

/* Static member definitions – included in exactly one TU via the header
   because they are inline-initialised. */
inline std::ofstream Logger::file_;
inline std::mutex    Logger::mutex_;
