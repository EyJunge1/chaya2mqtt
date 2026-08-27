#pragma once

#include "display_config.h"
#include "view_state.h"

#include <cstdint>

/** Which Lucide heart glyph the STA heart screen should show. */
enum class DisplayHeartIcon : uint8_t {
    Filled = 0,
    Crack  = 1,
};

/** Tracks continuous Wi-Fi/MQTT outage for the heart-crack grace period. */
struct DisplayLinkState {
    unsigned long outageSinceMs = 0UL;
};

/** STA is "online" only when Wi-Fi and MQTT are both up. AP/setup ignores link health. */
inline bool displayLinkIsOnline(bool apMode, bool wifiOk, bool mqttOk) {
    if (apMode) {
        return true;
    }
    return wifiOk && mqttOk;
}

/**
 * Decide filled vs crack heart.
 * Starts the outage timer on the first unhealthy sample; switches to crack after
 * graceMs of continuous outage; returns to filled immediately when healthy again.
 * Millis wrap is handled via unsigned subtraction.
 */
inline DisplayHeartIcon displayHeartIconDecide(bool apMode, bool wifiOk, bool mqttOk,
                                               unsigned long nowMs, unsigned long graceMs,
                                               DisplayLinkState& st) {
    if (displayLinkIsOnline(apMode, wifiOk, mqttOk)) {
        st.outageSinceMs = 0UL;
        return DisplayHeartIcon::Filled;
    }

    if (st.outageSinceMs == 0UL) {
        st.outageSinceMs = (nowMs == 0UL) ? 1UL : nowMs;
        return DisplayHeartIcon::Filled;
    }

    const unsigned long downFor = nowMs - st.outageSinceMs;
    if (downFor >= graceMs) {
        return DisplayHeartIcon::Crack;
    }
    return DisplayHeartIcon::Filled;
}

inline DisplayView displayViewForHeartIcon(DisplayHeartIcon icon) {
    return icon == DisplayHeartIcon::Crack ? DisplayView::HeartCrack : DisplayView::Heart;
}
