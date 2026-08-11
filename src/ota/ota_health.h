#pragma once

#include <cstdint>

/** Stable runtime after WiFi boot settles before cancelling OTA rollback. */
constexpr unsigned long kOtaHealthStableMs = 30000UL;

/**
 * Pure helper: true once setup is complete, boot WiFi settled, and
 * @p windowMs have elapsed since @p settledAtMs (wrap-safe via unsigned subtract).
 */
inline bool otaHealthWindowElapsed(bool setupComplete, bool bootSettled, unsigned long settledAtMs,
                                   unsigned long nowMs,
                                   unsigned long windowMs = kOtaHealthStableMs) {
    if (!setupComplete || !bootSettled || settledAtMs == 0UL) {
        return false;
    }
    return (nowMs - settledAtMs) >= windowMs;
}
