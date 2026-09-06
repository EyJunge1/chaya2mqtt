#pragma once

#include <cstdint>
#include <ctime>

/** Milliseconds elapsed since startMs (wrap-safe). */
inline auto elapsedMs(uint32_t startMs, uint32_t nowMs) -> uint32_t { return nowMs - startMs; }

/** True when nowMs is at or past startMs + durationMs (wrap-safe). */
inline auto deadlineReached(uint32_t startMs, uint32_t durationMs, uint32_t nowMs) -> bool {
    return elapsedMs(startMs, nowMs) >= durationMs;
}

/** Remaining ms until startMs + durationMs; 0 if past deadline (wrap-safe). */
inline auto remainingMs(uint32_t startMs, uint32_t durationMs, uint32_t nowMs) -> uint32_t {
    const uint32_t elapsed = elapsedMs(startMs, nowMs);
    return (elapsed >= durationMs) ? 0U : (durationMs - elapsed);
}

inline auto calendarDaySinceEpochUtc(time_t utc) -> uint32_t {
    if (utc < 0) {
        utc = 0;
    }
    return static_cast<uint32_t>(static_cast<uint64_t>(utc) / 86400ULL);
}
