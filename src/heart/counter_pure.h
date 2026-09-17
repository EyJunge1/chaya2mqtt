#pragma once

#include <algorithm>
#include <climits>
#include <cstdint>

#include "display/display_config.h"

/** Non-negative display delta: max(0, raw - baseline). */
inline auto heartCounterDeltaPure(int raw, int baseline) -> int { return (raw > baseline) ? (raw - baseline) : 0; }

/**
 * Format-side capped delta used by the E-Ink footer.
 * Intermediate clamp matches draw.cpp (0..9999), then "999+" when above kDisplayCounterMax.
 */
inline auto heartCounterShownDeltaPure(int raw, int baseline) -> int {
    const int64_t delta64 = static_cast<int64_t>(raw) - static_cast<int64_t>(baseline);
    const int64_t shown64 = std::max<int64_t>(0, std::min<int64_t>(delta64, 9999));
    if (shown64 > static_cast<int64_t>(kDisplayCounterMax)) {
        return kDisplayCounterMax + 1; // sentinel: UI prints "999+"
    }
    return static_cast<int>(shown64);
}

inline auto heartCounterShouldShowPlusPure(int raw, int baseline) -> bool {
    return heartCounterShownDeltaPure(raw, baseline) > kDisplayCounterMax;
}

/** Saturating TX increment after a successful publish. */
/** ACK/DATA must not apply counters or play audio during shutdown or factory NVS suspend (RC-MQTT-01). */
inline auto heartApplyAllowed(bool shutdown, bool nvsSuspended) -> bool { return !shutdown && !nvsSuspended; }

/** Re-check the apply gate at the store (RC-MQTT-12). */
inline auto heartSentApplyIfAllowed(int *dest, int expected, bool shutdown, bool nvsSuspended) -> bool {
    if (dest == nullptr || !heartApplyAllowed(shutdown, nvsSuspended)) {
        return false;
    }
    *dest = expected;
    return true;
}

inline auto heartSentCounterNextPure(int current) -> int {
    if (current >= INT_MAX) {
        return INT_MAX;
    }
    return current + 1;
}
