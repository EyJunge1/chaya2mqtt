#pragma once

#include <cstdint>

#include "async/sse_dirty.h"

/**
 * Decide which SSE domains to gather this tick (PERF-03).
 * @return bits to gather; 0 means skip gather entirely.
 */
inline uint32_t sseTickSelectBits(uint32_t pendingBits, uint32_t nowMs, uint32_t lastWorkMs,
                                  uint32_t keepaliveMs, bool* outKeepalive) {
    if (outKeepalive != nullptr) {
        *outKeepalive = false;
    }
    if (pendingBits != 0U) {
        return pendingBits;
    }
    if (keepaliveMs == 0U) {
        return 0U;
    }
    if (lastWorkMs == 0U || (nowMs - lastWorkMs) >= keepaliveMs) {
        if (outKeepalive != nullptr) {
            *outKeepalive = true;
        }
        return kSseWifi | kSseDevice;
    }
    return 0U;
}
