#pragma once

#include <IPAddress.h>
#include <cstddef>
#include <cstdio>

// IPv4 to "a.b.c.d" if bufLen >= 16.
inline void formatIpv4ToBuf(const IPAddress& ip, char* buf, size_t bufLen) {
    if (buf == nullptr || bufLen == 0U) {
        return;
    }
    snprintf(buf, bufLen, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
}
