#pragma once

#include "battery_config.h"

/** Piecewise-linear LiPo estimate from pack millivolts (0–100). */
inline int batteryPctFromMilliVolts(int mv) {
    struct Point {
        int mv;
        int pct;
    };
    static constexpr Point kCurve[] = {
        {3300, 0},  {3500, 5},  {3600, 10}, {3700, 20}, {3800, 35},
        {3900, 55}, {4000, 75}, {4100, 90}, {4200, 100},
    };
    if (mv <= kCurve[0].mv) {
        return 0;
    }
    constexpr int kLast = static_cast<int>(sizeof(kCurve) / sizeof(kCurve[0]) - 1U);
    if (mv >= kCurve[kLast].mv) {
        return 100;
    }
    for (int i = 0; i < kLast; ++i) {
        if (mv <= kCurve[i + 1].mv) {
            const int span = kCurve[i + 1].mv - kCurve[i].mv;
            const int rise = kCurve[i + 1].pct - kCurve[i].pct;
            const int d    = mv - kCurve[i].mv;
            return kCurve[i].pct + (span > 0 ? (rise * d) / span : 0);
        }
    }
    return 100;
}

inline bool batteryWarnLow(int pct) {
    return pct < kBatteryWarnPct;
}

inline bool batteryCriticalLow(int pct) {
    return pct < kBatteryCriticalPct;
}
