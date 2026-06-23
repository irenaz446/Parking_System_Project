/**
 * @file Session.hpp
 * @brief Represents one active (unresolved) parking session on the server.
 *
 * A Session is created when the server processes an MSG_START message and
 * destroyed (after fee calculation) when the matching MSG_END arrives.
 */

#pragma once

#include <string>
#include <ctime>
#include "../common/common.h"

class Session {
public:
    Session() = default;

    Session(const std::string &customerId,
            double lat, double lon,
            const std::string &city,
            time_t startTime)
        : customerId_(customerId)
        , startLat_(lat)
        , startLon_(lon)
        , city_(city)
        , startTime_(startTime)
    {}

    const std::string &customerId() const { return customerId_; }
    const std::string &city()       const { return city_; }
    double  startLat()  const { return startLat_; }
    double  startLon()  const { return startLon_; }
    time_t  startTime() const { return startTime_; }

    /** @brief Elapsed minutes from start until now. */
    double elapsedMinutes() const
    {
        return std::difftime(std::time(nullptr), startTime_) / 60.0;
    }

private:
    std::string customerId_;
    double      startLat_   = 0.0;
    double      startLon_   = 0.0;
    std::string city_;
    time_t      startTime_  = 0;
};
