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
 * (trailing edge), or skip because raw counters, shown deltas, heart icon, and
 * battery icon already match the last painted frame.
 *
 * Shown deltas default to 0/0 so older call sites stay raw-only. After a
 * baseline roll the raw values match but shown drops (999+ → 0) — that must
 * QueueNow / DeferPending, not SkipUnchanged.
 */
inline auto displayHeartRedrawDecide(int currentRx, int currentTx, int lastDrawnRx, int lastDrawnTx, bool iconChanged,
                                     bool batteryIconChanged, unsigned long nowMs, unsigned long lastEnqueueMs,
                                     unsigned long minIntervalMs, int currentShownRx = 0, int currentShownTx = 0,
                                     int lastDrawnShownRx = 0, int lastDrawnShownTx = 0) -> DisplayHeartRedrawDecision {
    if (currentRx == lastDrawnRx && currentTx == lastDrawnTx && currentShownRx == lastDrawnShownRx &&
        currentShownTx == lastDrawnShownTx && !iconChanged && !batteryIconChanged) {
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
/** SkipUnchanged must drop a deferred pending flag (BUG-UI-02). */
inline auto displayHeartSkipClearsPending(DisplayHeartRedrawDecision decision) -> bool {
    return decision == DisplayHeartRedrawDecision::SkipUnchanged;
}

/** Clear pending only when a re-read is still SkipUnchanged (RC-UI-02). */
inline auto displayHeartSkipClearsPendingAfterReread(DisplayHeartRedrawDecision first,
                                                     DisplayHeartRedrawDecision second) -> bool {
    return displayHeartSkipClearsPending(first) && displayHeartSkipClearsPending(second);
}

inline auto displayHeartRedrawWaitMs(unsigned long nowMs, unsigned long lastEnqueueMs, unsigned long minIntervalMs,
                                     bool pending) -> unsigned long {
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
inline auto displayHeartNeedsFollowUpRedraw(int drawnRx, int drawnTx, int currentRx, int currentTx, bool iconChanged,
                                            bool batteryIconChanged, bool hadPending, int drawnShownRx = 0, int drawnShownTx = 0,
                                            int currentShownRx = 0, int currentShownTx = 0) -> bool {
    return hadPending || iconChanged || batteryIconChanged || currentRx != drawnRx || currentTx != drawnTx ||
           currentShownRx != drawnShownRx || currentShownTx != drawnShownTx;
}
