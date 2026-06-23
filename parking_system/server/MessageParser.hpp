/**
 * @file MessageParser.hpp
 * @brief Parses raw TCP byte strings into wire_msg_t structs.
 *
 * Wire format (newline-terminated):
 *   "<S|E>|<CUSTOMER_ID>|<LAT>,<LON>|<CITY>"
 *
 * Example:
 *   "S|VH-001|32.085300,34.781700|TelAviv\n"
 *   "E|VH-001|32.086000,34.782000|TelAviv\n"
 */

#pragma once

#include <string>
#include <optional>
#include <sstream>
#include "../common/common.h"

class MessageParser {
public:
    /**
     * @brief Parse one line (newline already stripped) into a wire_msg_t.
     * @param line  Null-terminated string without trailing newline.
     * @return Populated wire_msg_t, or std::nullopt on parse error.
     */
    static std::optional<wire_msg_t> parse(const std::string &line)
    {
        wire_msg_t msg{};
        // Expected tokens separated by '|': TYPE | ID | LAT,LON | CITY
        std::istringstream ss(line);
        std::string token;

        // Token 1: type
        if (!std::getline(ss, token, '|')) return std::nullopt;
        if (token.size() != 1 ||
            (token[0] != MSG_START && token[0] != MSG_END))
            return std::nullopt;
        msg.type = token[0];

        // Token 2: customer_id
        if (!std::getline(ss, token, '|')) return std::nullopt;
        if (token.empty()) return std::nullopt;
        strncpy(msg.customer_id, token.c_str(), CUSTOMER_ID_LEN - 1);

        // Token 3: LAT,LON
        if (!std::getline(ss, token, '|')) return std::nullopt;
        auto comma = token.find(',');
        if (comma == std::string::npos) return std::nullopt;
        try {
            msg.lat = std::stod(token.substr(0, comma));
            msg.lon = std::stod(token.substr(comma + 1));
        } catch (...) { return std::nullopt; }

        // Token 4: city
        if (!std::getline(ss, token)) return std::nullopt;
        if (token.empty()) return std::nullopt;
        strncpy(msg.city, token.c_str(), CITY_NAME_LEN - 1);

        return msg;
    }
};
