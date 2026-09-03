#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

/** IPv4 dotted-quad buffer including NUL (`a.b.c.d`). */
constexpr size_t kIpv4StrMaxLen = 16U;

/** Parse dotted IPv4 into 4 octets. Rejects leading zeros (except "0"), spaces, extras. */
inline bool parseIpv4Dotted(const char *s, uint8_t out[4]) {
    if (s == nullptr || out == nullptr || s[0] == '\0') {
        return false;
    }
    unsigned parts[4]{};
    size_t idx = 0;
    const char *p = s;
    while (idx < 4U) {
        if (*p < '0' || *p > '9') {
            return false;
        }
        unsigned v = 0;
        size_t digits = 0;
        while (*p >= '0' && *p <= '9') {
            if (digits > 0U && v == 0U) {
                return false; // leading zero
            }
            v = v * 10U + static_cast<unsigned>(*p - '0');
            if (v > 255U) {
                return false;
            }
            ++digits;
            ++p;
            if (digits > 3U) {
                return false;
            }
        }
        if (digits == 0U) {
            return false;
        }
        parts[idx++] = v;
        if (idx < 4U) {
            if (*p != '.') {
                return false;
            }
            ++p;
        }
    }
    if (*p != '\0') {
        return false;
    }
    for (size_t i = 0; i < 4U; ++i) {
        out[i] = static_cast<uint8_t>(parts[i]);
    }
    return true;
}

inline bool ipv4SyntaxOk(const char *s) {
    uint8_t tmp[4];
    return parseIpv4Dotted(s, tmp);
}

/** True for 0.0.0.0. */
inline bool ipv4IsZero(const uint8_t ip[4]) { return ip != nullptr && ip[0] == 0U && ip[1] == 0U && ip[2] == 0U && ip[3] == 0U; }

/** Contiguous netmask (ones followed by zeros), not 0.0.0.0 and not host bits-only. */
inline bool ipv4NetmaskContiguousOk(const uint8_t mask[4]) {
    if (mask == nullptr || ipv4IsZero(mask)) {
        return false;
    }
    uint32_t m = (static_cast<uint32_t>(mask[0]) << 24) | (static_cast<uint32_t>(mask[1]) << 16) |
                 (static_cast<uint32_t>(mask[2]) << 8) | static_cast<uint32_t>(mask[3]);
    if (m == 0xFFFFFFFFU) {
        return false;
    }
    // After rightmost 1, only zeros remain <=> (m | (m - 1)) + 1 is power of two chain.
    // Contiguous ones from MSB: ~m has only low bits set, i.e. (~m + 1) is power of two or 0.
    const uint32_t inv = ~m;
    return (inv & (inv + 1U)) == 0U;
}

inline uint32_t ipv4ToU32(const uint8_t ip[4]) {
    return (static_cast<uint32_t>(ip[0]) << 24) | (static_cast<uint32_t>(ip[1]) << 16) | (static_cast<uint32_t>(ip[2]) << 8) |
           static_cast<uint32_t>(ip[3]);
}

inline bool ipv4SameSubnet(const uint8_t ip[4], const uint8_t gateway[4], const uint8_t mask[4]) {
    if (ip == nullptr || gateway == nullptr || mask == nullptr) {
        return false;
    }
    const uint32_t m = ipv4ToU32(mask);
    return (ipv4ToU32(ip) & m) == (ipv4ToU32(gateway) & m);
}

/**
 * Hostname or IPv4 for NTP/DNS/MQTT host fields: non-empty, printable ASCII, no spaces/wildcards.
 * Empty string is rejected (use optional-field handling at call site).
 */
inline bool hostFieldSyntaxOk(const char *host, size_t maxLen) {
    if (host == nullptr || host[0] == '\0' || maxLen == 0U) {
        return false;
    }
    size_t len = 0;
    for (const char *p = host; *p != '\0'; ++p) {
        ++len;
        if (len >= maxLen) {
            return false;
        }
        const unsigned char c = static_cast<unsigned char>(*p);
        if (c < 0x20U || c > 0x7EU || c == ' ' || c == '#' || c == '+') {
            return false;
        }
    }
    return true;
}

inline bool ntpHostSyntaxOk(const char *host, size_t maxLen) { return hostFieldSyntaxOk(host, maxLen); }

inline void formatIpv4Octets(const uint8_t ip[4], char *buf, size_t bufLen) {
    if (buf == nullptr || bufLen == 0U) {
        return;
    }
    if (ip == nullptr) {
        buf[0] = '\0';
        return;
    }
    snprintf(buf, bufLen, "%u.%u.%u.%u", static_cast<unsigned>(ip[0]), static_cast<unsigned>(ip[1]), static_cast<unsigned>(ip[2]),
             static_cast<unsigned>(ip[3]));
}
