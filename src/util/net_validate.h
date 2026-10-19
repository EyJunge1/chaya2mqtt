#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "util/format_buf.h"

/** IPv4 dotted-quad buffer including NUL (`a.b.c.d`). */
constexpr size_t kIpv4StrMaxLen = 16U;

/** Parse dotted IPv4 into 4 octets. Rejects leading zeros (except "0"), spaces, extras. */
[[nodiscard]] inline auto parseIpv4Dotted(std::string_view s, std::span<uint8_t, 4> out) -> bool {
    if (s.empty()) {
        return false;
    }
    unsigned parts[4]{};
    size_t idx = 0;
    size_t pos = 0;
    while (idx < 4U) {
        if (pos >= s.size() || s[pos] < '0' || s[pos] > '9') {
            return false;
        }
        unsigned v = 0;
        size_t digits = 0;
        while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') {
            if (digits > 0U && v == 0U) {
                return false; // leading zero
            }
            v = v * 10U + static_cast<unsigned>(s[pos] - '0');
            if (v > 255U) {
                return false;
            }
            ++digits;
            ++pos;
            if (digits > 3U) {
                return false;
            }
        }
        if (digits == 0U) {
            return false;
        }
        parts[idx++] = v;
        if (idx < 4U) {
            if (pos >= s.size() || s[pos] != '.') {
                return false;
            }
            ++pos;
        }
    }
    if (pos != s.size()) {
        return false;
    }
    for (size_t i = 0; i < 4U; ++i) {
        out[i] = static_cast<uint8_t>(parts[i]);
    }
    return true;
}

[[nodiscard]] inline auto parseIpv4Dotted(const char *s, uint8_t out[4]) -> bool {
    if (s == nullptr || out == nullptr) {
        return false;
    }
    return parseIpv4Dotted(std::string_view{s}, std::span<uint8_t, 4>{out, 4});
}

/** True for 0.0.0.0. */
[[nodiscard]] constexpr auto ipv4IsZero(std::span<const uint8_t, 4> ip) -> bool {
    return ip[0] == 0U && ip[1] == 0U && ip[2] == 0U && ip[3] == 0U;
}

[[nodiscard]] inline auto ipv4IsZero(const uint8_t ip[4]) -> bool {
    return ip != nullptr && ipv4IsZero(std::span<const uint8_t, 4>{ip, 4});
}

/** Contiguous netmask (ones followed by zeros), not 0.0.0.0 and not host bits-only. */
[[nodiscard]] constexpr auto ipv4NetmaskContiguousOk(std::span<const uint8_t, 4> mask) -> bool {
    if (ipv4IsZero(mask)) {
        return false;
    }
    const uint32_t m = (static_cast<uint32_t>(mask[0]) << 24U) | (static_cast<uint32_t>(mask[1]) << 16U) |
                       (static_cast<uint32_t>(mask[2]) << 8U) | static_cast<uint32_t>(mask[3]);
    if (m == 0xFFFFFFFFU) {
        return false;
    }
    const uint32_t inv = ~m;
    return (inv & (inv + 1U)) == 0U;
}

[[nodiscard]] inline auto ipv4NetmaskContiguousOk(const uint8_t mask[4]) -> bool {
    return mask != nullptr && ipv4NetmaskContiguousOk(std::span<const uint8_t, 4>{mask, 4});
}

[[nodiscard]] constexpr auto ipv4ToU32(std::span<const uint8_t, 4> ip) -> uint32_t {
    return (static_cast<uint32_t>(ip[0]) << 24U) | (static_cast<uint32_t>(ip[1]) << 16U) | (static_cast<uint32_t>(ip[2]) << 8U) |
           static_cast<uint32_t>(ip[3]);
}

[[nodiscard]] inline auto ipv4ToU32(const uint8_t ip[4]) -> uint32_t { return ipv4ToU32(std::span<const uint8_t, 4>{ip, 4}); }

[[nodiscard]] constexpr auto ipv4SameSubnet(std::span<const uint8_t, 4> ip, std::span<const uint8_t, 4> gateway,
                                            std::span<const uint8_t, 4> mask) -> bool {
    const uint32_t m = ipv4ToU32(mask);
    return (ipv4ToU32(ip) & m) == (ipv4ToU32(gateway) & m);
}

[[nodiscard]] inline auto ipv4SameSubnet(const uint8_t ip[4], const uint8_t gateway[4], const uint8_t mask[4]) -> bool {
    if (ip == nullptr || gateway == nullptr || mask == nullptr) {
        return false;
    }
    return ipv4SameSubnet(std::span<const uint8_t, 4>{ip, 4}, std::span<const uint8_t, 4>{gateway, 4},
                          std::span<const uint8_t, 4>{mask, 4});
}

/**
 * Hostname or IPv4 for NTP/DNS/MQTT host fields: non-empty, printable ASCII, no spaces/wildcards.
 * Empty string is rejected (use optional-field handling at call site).
 */
[[nodiscard]] inline auto hostFieldSyntaxOk(std::string_view host, size_t maxLen) -> bool {
    if (host.empty() || maxLen == 0U || host.size() >= maxLen) {
        return false;
    }
    for (const char cRaw : host) {
        const auto c = static_cast<unsigned char>(cRaw);
        if (c < 0x20U || c > 0x7EU || c == ' ' || c == '#' || c == '+') {
            return false;
        }
    }
    return true;
}

[[nodiscard]] inline auto hostFieldSyntaxOk(const char *host, size_t maxLen) -> bool {
    if (host == nullptr) {
        return false;
    }
    return hostFieldSyntaxOk(std::string_view{host}, maxLen);
}

[[nodiscard]] inline auto ntpHostSyntaxOk(const char *host, size_t maxLen) -> bool { return hostFieldSyntaxOk(host, maxLen); }

inline void formatIpv4Octets(std::span<const uint8_t, 4> ip, std::span<char> buf) {
    if (buf.empty()) {
        return;
    }
    static_cast<void>(formatToBuf(buf, "{}.{}.{}.{}", static_cast<unsigned>(ip[0]), static_cast<unsigned>(ip[1]),
                                  static_cast<unsigned>(ip[2]), static_cast<unsigned>(ip[3])));
}

inline void formatIpv4Octets(const uint8_t ip[4], char *buf, size_t bufLen) {
    if (buf == nullptr || bufLen == 0U) {
        return;
    }
    if (ip == nullptr) {
        buf[0] = '\0';
        return;
    }
    formatIpv4Octets(std::span<const uint8_t, 4>{ip, 4}, std::span<char>{buf, bufLen});
}
