#pragma once

/** Shared GPIO debounce fields (host-testable). */
struct DebouncedGpioState {
    int lastRawReading = 0;
    unsigned long lastDebounceChangeMs = 0;
    int debouncedLevel = 0;
};

/**
 * Update debounce state from a raw GPIO sample.
 * Level only commits after `stableMs` without change.
 */
inline void debounceUpdate(DebouncedGpioState &state, int raw, unsigned long nowMs, unsigned long stableMs) {
    if (raw != state.lastRawReading) {
        state.lastRawReading = raw;
        state.lastDebounceChangeMs = nowMs;
    }
    if (nowMs - state.lastDebounceChangeMs >= stableMs) {
        state.debouncedLevel = state.lastRawReading;
    }
}
