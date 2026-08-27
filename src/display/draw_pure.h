#pragma once

#include <cstdint>

enum class DisplayBatteryColor : uint8_t {
    Black,
    Yellow,
    Red,
};

inline DisplayBatteryColor displayBatteryColor(int batteryPct) {
    if (batteryPct < 15) {
        return DisplayBatteryColor::Red;
    }
    if (batteryPct < 40) {
        return DisplayBatteryColor::Yellow;
    }
    return DisplayBatteryColor::Black;
}
