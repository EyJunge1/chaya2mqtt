#pragma once

#include <climits>
#include <cstdint>

/** Min interval between heart redraw enqueues (leading edge). */
constexpr unsigned long kHeartRedrawMinIntervalMs = 20000UL;

enum class DisplayHeartRedrawDecision : uint8_t {
    SkipUnchanged = 0,
    QueueNow = 1,
    DeferPending = 2,
};

/**
 * Decide whether a heart redraw should queue now, wait for the min interval
 * (trailing edge), or skip because counters, heart icon, and battery icon
 * already match the last painted frame.
 */
inline DisplayHeartRedrawDecision displayHeartRedrawDecide(int currentRx, int currentTx, int lastDrawnRx, int lastDrawnTx,
                                                           bool iconChanged, bool batteryIconChanged, unsigned long nowMs,
                                                           unsigned long lastEnqueueMs, unsigned long minIntervalMs) {
    if (currentRx == lastDrawnRx && currentTx == lastDrawnTx && !iconChanged && !batteryIconChanged) {
        return DisplayHeartRedrawDecision::SkipUnchanged;
    }
    if (lastEnqueueMs != 0UL && (nowMs - lastEnqueueMs) < minIntervalMs) {
        return DisplayHeartRedrawDecision::DeferPending;
    }
    return DisplayHeartRedrawDecision::QueueNow;
}

/**
 * How long the display task should wait for the next command when a deferred
 * heart redraw is pending. ULONG_MAX means wait forever (no pending work).
 */
inline unsigned long displayHeartRedrawWaitMs(unsigned long nowMs, unsigned long lastEnqueueMs, unsigned long minIntervalMs,
                                              bool pending) {
    if (!pending) {
        return ULONG_MAX;
    }
    if (lastEnqueueMs == 0UL) {
        return 0UL;
    }
    const unsigned long elapsed = nowMs - lastEnqueueMs;
    if (elapsed >= minIntervalMs) {
        return 0UL;
    }
    return minIntervalMs - elapsed;
}

/** True when a follow-up redraw is needed after a completed heart paint. */
inline bool displayHeartNeedsFollowUpRedraw(int drawnRx, int drawnTx, int currentRx, int currentTx, bool iconChanged,
                                            bool batteryIconChanged, bool hadPending) {
    return hadPending || iconChanged || batteryIconChanged || currentRx != drawnRx || currentTx != drawnTx;
}
