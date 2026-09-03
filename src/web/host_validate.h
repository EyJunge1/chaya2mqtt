#pragma once

#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstring>

/** Case-insensitive full-string host equality. */
inline bool hostEqualsIgnoreCase(const char *host, const char *ref) {
    if (host == nullptr || ref == nullptr) {
        return false;
    }
    while (*host != '\0' && *ref != '\0') {
        if (std::tolower(static_cast<unsigned char>(*host)) != std::tolower(static_cast<unsigned char>(*ref))) {
            return false;
        }
        ++host;
        ++ref;
    }
    return *host == '\0' && *ref == '\0';
}

/** True when host equals prefix (case-insensitive) or prefix followed by ':port'. */
inline bool hostPrefixIgnoreCaseThenPortOrEnd(const char *host, const char *prefix) {
    if (host == nullptr || prefix == nullptr) {
        return false;
    }
    while (*prefix != '\0') {
        if (std::tolower(static_cast<unsigned char>(*host)) != std::tolower(static_cast<unsigned char>(*prefix))) {
            return false;
        }
        ++host;
        ++prefix;
    }
    return *host == '\0' || *host == ':';
}

/**
 * Host allow-list (pure logic).
 * @param host request host (may include :port)
 * @param apMode captive portal: setup IP and hostname allowlist only
 * @param deviceHostname current station hostname (without .local)
 * @param staIp optional STA IPv4 string; nullptr/empty skips IP match
 */
inline bool webHostCStringAllowed(const char *host, bool apMode, const char *deviceHostname, const char *staIp) {
    if (host == nullptr || host[0] == '\0') {
        // HTTP/1.1 requires Host. Keep hostless HTTP/1.0 captive probes working only in AP mode.
        return apMode;
    }
    if (apMode) {
        // SEC-04: SoftAP allowlist only — setup IP / captive hostname (not arbitrary Host).
        if (hostEqualsIgnoreCase(host, "4.3.2.1")) {
            return true;
        }
        if (hostPrefixIgnoreCaseThenPortOrEnd(host, "4.3.2.1")) {
            return true;
        }
        if (hostEqualsIgnoreCase(host, "chaya2mqtt")) {
            return true;
        }
        if (hostPrefixIgnoreCaseThenPortOrEnd(host, "chaya2mqtt.local")) {
            return true;
        }
        return false;
    }
    if (deviceHostname == nullptr || deviceHostname[0] == '\0') {
        return false;
    }
    if (hostEqualsIgnoreCase(host, deviceHostname)) {
        return true;
    }
    char localPrefix[48];
    static_cast<void>(std::snprintf(localPrefix, sizeof(localPrefix), "%s.local", deviceHostname));
    if (hostPrefixIgnoreCaseThenPortOrEnd(host, localPrefix)) {
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
