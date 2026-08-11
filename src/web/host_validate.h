#pragma once

#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "constants.h"

/** Case-insensitive full-string host equality. */
inline bool hostEqualsIgnoreCase(const char* host, const char* ref) {
    if (host == nullptr || ref == nullptr) {
        return false;
    }
    while (*host != '\0' && *ref != '\0') {
        if (std::tolower(static_cast<unsigned char>(*host))
            != std::tolower(static_cast<unsigned char>(*ref))) {
            return false;
        }
        ++host;
        ++ref;
    }
    return *host == '\0' && *ref == '\0';
}

/** True when host equals prefix (case-insensitive) or prefix followed by ':port'. */
inline bool hostPrefixIgnoreCaseThenPortOrEnd(const char* host, const char* prefix) {
    if (host == nullptr || prefix == nullptr) {
        return false;
    }
    while (*prefix != '\0') {
        if (std::tolower(static_cast<unsigned char>(*host))
            != std::tolower(static_cast<unsigned char>(*prefix))) {
            return false;
        }
        ++host;
        ++prefix;
    }
    return *host == '\0' || *host == ':';
}

/**
 * Host allow-list for Host / Origin checks (pure logic).
 * @param host request host (may include :port)
 * @param apMode captive portal: allow any host
 * @param staIp optional STA IPv4 string; nullptr/empty skips IP match
 */
inline bool webHostCStringAllowed(const char* host, bool apMode, const char* staIp) {
    if (host == nullptr || host[0] == '\0') {
        return true;
    }
    if (hostEqualsIgnoreCase(host, kDeviceHostname)) {
        return true;
    }
    char localPrefix[48];
    static_cast<void>(
        std::snprintf(localPrefix, sizeof(localPrefix), "%s.local", kDeviceHostname));
    if (hostPrefixIgnoreCaseThenPortOrEnd(host, localPrefix)) {
        return true;
    }
    if (apMode) {
        return true;
    }
    if (staIp != nullptr && staIp[0] != '\0') {
        if (hostEqualsIgnoreCase(host, staIp)) {
            return true;
        }
        const size_t ipLen = std::strlen(staIp);
        if (std::strncmp(host, staIp, ipLen) == 0 && host[ipLen] == ':') {
            return true;
        }
    }
    return false;
}
