#pragma once

/**
 * EXT1 ANY_LOW is level-triggered. Arm only when PWR is not already LOW
 * (Arduino digitalRead / rtc_gpio_get_level: LOW = 0).
 */
inline bool softOffMayArmExt1Wake(int pwrLevel) {
    return pwrLevel != 0;
}

/** Continuous HIGH timer for the pre-EXT1 bounce settle. */
struct SoftOffReleaseSettle {
    unsigned long highSinceMs = 0;
};

/** True once `pwrLevel` has been inactive (not LOW) for `settleMs`. Resets on LOW. */
inline bool softOffReleaseSettled(SoftOffReleaseSettle& st, int pwrLevel, unsigned long nowMs,
                                  unsigned long settleMs) {
    if (!softOffMayArmExt1Wake(pwrLevel)) {
        st.highSinceMs = 0;
        return false;
    }
    if (st.highSinceMs == 0) {
        st.highSinceMs = nowMs;
        return false;
    }
    return nowMs - st.highSinceMs >= settleMs;
}
