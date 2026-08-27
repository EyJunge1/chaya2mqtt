#pragma once

#include "hw/battery_pure.h"

inline bool displayBatteryIconYellow(int batteryPct) {
    return batteryWarnLow(batteryPct);
}
