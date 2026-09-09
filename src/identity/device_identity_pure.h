#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "constants.h"

/** How `buildDeviceId` obtains a missing / invalid NVS value. */
enum class DeviceIdCreateMode : uint8_t {
    FromMacMigrate = 0,
    FromRandom = 1,
};

/**
 * Prefer MAC seed when prior WiFi/MQTT setup exists (OTA upgrade path);
 * otherwise create a fresh random ID (factory reset / erase / first boot).
 */
inline auto deviceIdCreateMode(bool hadPriorSetupConfig) -> DeviceIdCreateMode {
    return hadPriorSetupConfig ? DeviceIdCreateMode::FromMacMigrate : DeviceIdCreateMode::FromRandom;
}

/** MQTT prior-setup is true for legacy server or cfg_v1 blob (RC-LIFE-02). */
inline auto hadPriorMqttSetupKeys(bool hasLegacyServer, bool hasCfgV1) -> bool {
    return hasLegacyServer || hasCfgV1;
}

/** Format 3 bytes as 6 lowercase hex chars; false if buffer too small or syntax fails. */
inline auto deviceIdFormatFromBytes(const uint8_t bytes[3], char *out, size_t outLen) -> bool {
    if (bytes == nullptr || out == nullptr || outLen < kDeviceIdBufLen) {
        if (out != nullptr && outLen > 0U) {
            out[0] = '\0';
        }
        return false;
    }
    const int n = std::snprintf(out, outLen, "%02x%02x%02x", bytes[0], bytes[1], bytes[2]);
    if (n != static_cast<int>(kDeviceIdHexLen) || !deviceIdSyntaxOk(out)) {
        out[0] = '\0';
        return false;
    }
    return true;
}
