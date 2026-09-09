#pragma once

#include <cstddef>
#include <cstdio>

#include "constants.h"

/**
 * Build this device's 6-char lowercase hex ID (NVS `cfg/device_id`).
 * Created randomly on first boot / after factory reset; OTA upgrades without
 * that key seed once from the STA MAC when WiFi/MQTT config already exists.
 */
void buildDeviceId(char *out, size_t outLen);

/** Drop the RAM device-id cache after factory NVS wipe (RC-LIFE-07). */
void deviceIdentityResetRamAfterFactoryClear();

/** Format the unique station / mDNS hostname from a validated device ID. */
inline auto formatDeviceStaHostname(const char *deviceId, char *out, size_t outLen) -> bool {
    if (out == nullptr || outLen == 0U) {
        return false;
    }
    out[0] = '\0';
    if (!deviceIdSyntaxOk(deviceId) || outLen < kDeviceStaHostnameBufLen) {
        return false;
    }
    const int n = std::snprintf(out, outLen, "%s%s", kDeviceStaHostnamePrefix, deviceId);
    return n > 0 && static_cast<size_t>(n) < outLen;
}

/** Build this device's unique station / mDNS hostname. */
inline auto buildDeviceStaHostname(char *out, size_t outLen) -> bool {
    char deviceId[kDeviceIdBufLen]{};
    buildDeviceId(deviceId, sizeof(deviceId));
    return formatDeviceStaHostname(deviceId, out, outLen);
}
