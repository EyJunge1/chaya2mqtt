#pragma once

#include "hw/battery_pure.h"

inline bool displayFreshRxDots(bool freshRxSinceLastDraw) {
    return freshRxSinceLastDraw;
}

inline bool displayBatteryIconYellow(int batteryPct) {
    return batteryWarnLow(batteryPct);
}
