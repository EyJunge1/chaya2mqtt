#pragma once

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>

/**
 * Parse a decimal counter payload (digits only, length 1..10).
 * @return true and sets *out when valid; false leaves *out unchanged.
 */
inline bool mqttParseCounterPayload(const char* payload, unsigned int length, long* out) {
    if (out == nullptr || payload == nullptr || length == 0U || length > 10U) {
        return false;
    }
    for (unsigned i = 0; i < length; ++i) {
        if (payload[i] < '0' || payload[i] > '9') {
            return false;
        }
    }
    char buf[12];
    std::memcpy(buf, payload, length);
    buf[length] = '\0';

    char*     endPtr  = nullptr;
    errno             = 0;
    const long parsed = std::strtol(buf, &endPtr, 10);
    if (errno == ERANGE || endPtr != buf + length || parsed < 0
        || parsed > static_cast<long>(INT_MAX)) {
        return false;
    }
    *out = parsed;
    return true;
}
