#pragma once

#include <cstdint>

#include "async/sse_dirty.h"

/**
 * Decide which SSE domains to gather this tick (PERF-03).
 * @return bits to gather; 0 means skip gather entirely.
 */
inline auto sseTickSelectBits(uint32_t pendingBits, uint32_t nowMs, uint32_t lastWorkMs, uint32_t keepaliveMs,
                              bool *outKeepalive) -> uint32_t {
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

/** SoftAP clients only get wifi/device SSE; STA REST already gates the rest (RC-WEB-03). */
inline auto sseTickMaskForApMode(uint32_t workBits, bool apMode) -> uint32_t {
    return apMode ? (workBits & (kSseWifi | kSseDevice)) : workBits;
}

/** Force a full snapshot from bits before the AP mask (BUG-WEB-51). */
inline auto sseTickForceSnapshot(uint32_t selectBits) -> bool { return (selectBits & kSseAll) == kSseAll; }
