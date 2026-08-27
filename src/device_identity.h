#pragma once

#include <cstddef>
#include <cstdio>

#include "constants.h"

/** Build this device's 6-char lowercase hex ID from the ESP32 STA MAC. */
void buildDeviceId(char* out, size_t outLen);

/** Format the unique station / mDNS hostname from a validated device ID. */
inline bool formatDeviceStaHostname(const char* deviceId, char* out, size_t outLen) {
    if (out == nullptr || outLen == 0U) {
        return false;
    }
    out[0] = '\0';
    if (!deviceIdSyntaxOk(deviceId) || outLen < kDeviceStaHostnameBufLen) {
        return false;
    }
    const int n =
        std::snprintf(out, outLen, "%s%s", kDeviceStaHostnamePrefix, deviceId);
    return n > 0 && static_cast<size_t>(n) < outLen;
}

/** Build this device's unique station / mDNS hostname. */
inline bool buildDeviceStaHostname(char* out, size_t outLen) {
    char deviceId[kDeviceIdBufLen]{};
    buildDeviceId(deviceId, sizeof(deviceId));
    return formatDeviceStaHostname(deviceId, out, outLen);
}
