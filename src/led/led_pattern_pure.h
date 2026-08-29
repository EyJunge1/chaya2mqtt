#pragma once

#include <cstdint>

/** Runtime for a finite on/off blink pattern (host-testable). */
struct LedPatternRuntime {
    uint8_t  onPulsesLeft = 0;
    bool     onPhase      = false;
    uint16_t onMs         = 0;
    uint16_t offMs        = 0;
};

struct LedPatternAdvanceResult {
    bool     done       = false;
    bool     ledOn      = false;
    uint16_t durationMs = 0;
};

/** Clamp invalid patterns: count/onMs at least 1. */
inline void ledPatternNormalize(uint8_t& count, uint16_t& onMs, uint16_t& /*offMs*/) {
    if (count == 0) {
        count = 1;
    }
    if (onMs == 0) {
        onMs = 1;
    }
}

/** Arm a pattern; returns false if count/onMs normalize to unusable (should not happen). */
inline bool ledPatternBegin(LedPatternRuntime& rt, uint8_t count, uint16_t onMs, uint16_t offMs) {
    ledPatternNormalize(count, onMs, offMs);
    rt.onPulsesLeft = count;
    rt.onPhase      = true;
    rt.onMs         = onMs;
    rt.offMs        = offMs;
    return true;
}

/**
 * Advance after the current phase duration has elapsed.
 * Ends dark. Trailing off after the last pulse is skipped when offMs == 0.
 */
inline LedPatternAdvanceResult ledPatternAdvance(LedPatternRuntime& rt) {
    if (rt.onPhase) {
        rt.onPulsesLeft = static_cast<uint8_t>(rt.onPulsesLeft - 1U);
        if (rt.onPulsesLeft == 0) {
            if (rt.offMs == 0) {
                return {true, false, 0};
            }
            rt.onPhase = false;
            return {false, false, rt.offMs};
        }
        if (rt.offMs == 0) {
            return {false, true, rt.onMs};
        }
        rt.onPhase = false;
        return {false, false, rt.offMs};
    }

    if (rt.onPulsesLeft == 0) {
        return {true, false, 0};
    }
    rt.onPhase = true;
    return {false, true, rt.onMs};
}
