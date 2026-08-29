#pragma once

#include "audio_config.h"

#include <cstdint>

/** Quiet window in local hours. Equal endpoints disable quiet hours. Wraps midnight. */
inline bool audioQuietHoursActive(uint8_t hour, uint8_t quietStart, uint8_t quietEnd) {
    if (quietStart > kAudioHourMax || quietEnd > kAudioHourMax) {
        return false;
    }
    if (quietStart == quietEnd) {
        return false;
    }
    if (quietStart < quietEnd) {
        return hour >= quietStart && hour < quietEnd;
    }
    return hour >= quietStart || hour < quietEnd;
}

inline bool audioPlaybackAllowed(bool kindEnabled, uint8_t volume, bool timeSynced, uint8_t hour,
                                 uint8_t quietStart, uint8_t quietEnd) {
    if (!kindEnabled || volume == 0U) {
        return false;
    }
    if (!timeSynced) {
        return true;
    }
    return !audioQuietHoursActive(hour, quietStart, quietEnd);
}
