#pragma once

#include <Arduino.h>

#include <atomic>
#include <cstdint>

#include "rate_limit_pure.h"

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
    return webMinIntervalAllowAt(lim.lastMs, lim.minIntervalMs, millis());
}
