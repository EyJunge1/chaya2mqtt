#pragma once

#include <Arduino.h>

#include <atomic>
#include <cstdint>

/**
 * Simple per-route minimum interval (token bucket with capacity 1).
 * Used to blunt spam on expensive / destructive admin APIs.
 */
struct WebMinIntervalLimit {
    uint32_t              minIntervalMs;
    std::atomic<uint32_t> lastMs{0};
};

/** Returns true if the call is allowed and records the attempt time. */
inline bool webMinIntervalAllow(WebMinIntervalLimit& lim) {
    const uint32_t now  = millis();
    uint32_t       prev = lim.lastMs.load(std::memory_order_relaxed);
    if (prev != 0U && (now - prev) < lim.minIntervalMs) {
        return false;
    }
    if (!lim.lastMs.compare_exchange_strong(prev, now, std::memory_order_acq_rel,
                                            std::memory_order_relaxed)) {
        prev = lim.lastMs.load(std::memory_order_relaxed);
        if (prev != 0U && (now - prev) < lim.minIntervalMs) {
            return false;
        }
        lim.lastMs.store(now, std::memory_order_relaxed);
    }
    return true;
}
