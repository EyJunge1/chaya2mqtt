#pragma once

#include <cstdint>

/** Stable STA runtime before cancelling OTA rollback. */
constexpr unsigned long kOtaHealthStableMs = 30000UL;

/**
 * Pure helper: true once setup is complete, boot WiFi settled, STA is linked,
 * and @p windowMs have elapsed since @p settledAtMs (wrap-safe via unsigned subtract).
 *
 * Without @p staConnected, never mark valid — keeps rollback available after a
 * boot that settles offline (ContinueStaOnly / STAB-03).
 */
inline bool otaHealthWindowElapsed(bool setupComplete, bool bootSettled, bool staConnected, unsigned long settledAtMs,
                                   unsigned long nowMs, unsigned long windowMs = kOtaHealthStableMs) {
    if (!setupComplete || !bootSettled || !staConnected || settledAtMs == 0UL) {
        return false;
    }
    return (nowMs - settledAtMs) >= windowMs;
}
