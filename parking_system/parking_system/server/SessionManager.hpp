/**
 * @file SessionManager.hpp
 * @brief Thread-safe store for active (unresolved) parking sessions.
 *
 * The server's poll loop runs on a single thread, but a SessionManager
 * instance is safe to use from multiple threads if needed (std::mutex).
 * Keyed by customer_id string.
 */

#pragma once

#include <string>
#include <unordered_map>
#include <mutex>
#include <optional>
#include "Session.hpp"

class SessionManager {
public:
    /** @brief Store a new session (overwrites any existing one for the same ID). */
    void start(const Session &s)
    {
        std::lock_guard<std::mutex> lock(mx_);
        sessions_[s.customerId()] = s;
    }

    /**
     * @brief Remove and return the session for customerId, if it exists.
     * @return The session, or std::nullopt if not found.
     */
    std::optional<Session> end(const std::string &customerId)
    {
        std::lock_guard<std::mutex> lock(mx_);
        auto it = sessions_.find(customerId);
        if (it == sessions_.end()) return std::nullopt;
        Session s = it->second;
        sessions_.erase(it);
        return s;
    }

    /** @brief Number of active sessions. */
    std::size_t size() const
    {
        std::lock_guard<std::mutex> lock(mx_);
        return sessions_.size();
    }

private:
    mutable std::mutex                          mx_;
    std::unordered_map<std::string, Session>    sessions_;
};
