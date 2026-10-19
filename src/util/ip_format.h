#pragma once

#include <IPAddress.h>
#include <cstddef>
#include <span>

#include "util/format_buf.h"

// IPv4 to "a.b.c.d" if bufLen >= 16.
inline void formatIpv4ToBuf(const IPAddress &ip, char *buf, size_t bufLen) {
    if (buf == nullptr || bufLen == 0U) {
        return;
    }
    static_cast<void>(formatToBuf(std::span<char>{buf, bufLen}, "{}.{}.{}.{}", ip[0], ip[1], ip[2], ip[3]));
}
