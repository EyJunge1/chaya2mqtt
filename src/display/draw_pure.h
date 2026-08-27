#pragma once

#include <cstdint>

enum class DisplayBatteryColor : uint8_t {
    Black,
    Yellow,
    Red,
};

/** Lucide battery glyph chosen to match the web dashboard thresholds. */
enum class DisplayBatteryIcon : uint8_t {
    Full   = 0, // >= 80%
    Medium = 1, // >= 40%
    Low    = 2, // >= 15%
    Empty  = 3, // < 15%
};

inline DisplayBatteryIcon displayBatteryIcon(int batteryPct) {
    if (batteryPct >= 80) {
        return DisplayBatteryIcon::Full;
    }
    if (batteryPct >= 40) {
        return DisplayBatteryIcon::Medium;
    }
    if (batteryPct >= 15) {
        return DisplayBatteryIcon::Low;
    }
    return DisplayBatteryIcon::Empty;
}

/**
 * E-Ink battery tint: Full/Medium black, Low yellow, Empty red.
 */
inline DisplayBatteryColor displayBatteryColor(int batteryPct) {
    switch (displayBatteryIcon(batteryPct)) {
    case DisplayBatteryIcon::Empty:
        return DisplayBatteryColor::Red;
    case DisplayBatteryIcon::Low:
        return DisplayBatteryColor::Yellow;
    case DisplayBatteryIcon::Full:
    case DisplayBatteryIcon::Medium:
        break;
    }
    return DisplayBatteryColor::Black;
}
