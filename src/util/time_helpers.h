#pragma once

#include <cstdint>

/** Milliseconds elapsed since startMs (wrap-safe). */
inline uint32_t elapsedMs(uint32_t startMs, uint32_t nowMs) {
    return nowMs - startMs;
}

/** True when nowMs is at or past startMs + durationMs (wrap-safe). */
inline bool deadlineReached(uint32_t startMs, uint32_t durationMs, uint32_t nowMs) {
    return elapsedMs(startMs, nowMs) >= durationMs;
}

/** Remaining ms until startMs + durationMs; 0 if past deadline (wrap-safe). */
inline uint32_t remainingMs(uint32_t startMs, uint32_t durationMs, uint32_t nowMs) {
    const uint32_t elapsed = elapsedMs(startMs, nowMs);
    return (elapsed >= durationMs) ? 0U : (durationMs - elapsed);
}
