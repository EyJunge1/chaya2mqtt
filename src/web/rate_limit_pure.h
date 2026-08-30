#pragma once

#include <atomic>
#include <cstdint>

/**
 * Pure minimum-interval gate (token bucket capacity 1).
 * @param lastMs atomic previous allow timestamp (0 = never)
 * @param minIntervalMs minimum spacing between allows
 * @param nowMs current time
 */
inline bool webMinIntervalAllowAt(std::atomic<uint32_t>& lastMs, uint32_t minIntervalMs,
                                  uint32_t nowMs) {
    uint32_t prev = lastMs.load(std::memory_order_relaxed);
    if (prev != 0U && (nowMs - prev) < minIntervalMs) {
        return false;
    }
    if (!lastMs.compare_exchange_strong(prev, nowMs, std::memory_order_acq_rel,
                                        std::memory_order_relaxed)) {
        prev = lastMs.load(std::memory_order_relaxed);
        if (prev != 0U && (nowMs - prev) < minIntervalMs) {
            return false;
        }
        lastMs.store(nowMs, std::memory_order_relaxed);
    }
    return true;
}
