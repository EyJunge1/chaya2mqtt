#pragma once

#include <cstddef>
#include <span>

#include "constants.h"
#include "util/format_buf.h"

/**
 * Build this device's 6-char lowercase hex ID (NVS `cfg/device_id`).
 * Created randomly on first boot / after factory reset when the key is missing.
 */
void buildDeviceId(char *out, size_t outLen);

/** Drop the RAM device-id cache after factory NVS wipe (RC-LIFE-07). */
void deviceIdentityResetRamAfterFactoryClear();

/** Format the unique station / mDNS hostname from a validated device ID. */
[[nodiscard]] inline auto formatDeviceStaHostname(const char *deviceId, char *out, size_t outLen) -> bool {
    if (out == nullptr || outLen == 0U) {
        return false;
    }
    out[0] = '\0';
    if (!deviceIdSyntaxOk(deviceId) || outLen < kDeviceStaHostnameBufLen) {
        return false;
    }
    return formatToBuf(std::span<char>{out, outLen}, "{}{}", kDeviceStaHostnamePrefix, deviceId);
}

/** Build this device's unique station / mDNS hostname. */
[[nodiscard]] inline auto buildDeviceStaHostname(char *out, size_t outLen) -> bool {
    char deviceId[kDeviceIdBufLen]{};
    buildDeviceId(deviceId, sizeof(deviceId));
    return formatDeviceStaHostname(deviceId, out, outLen);
}
