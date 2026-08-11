#pragma once

#include <cstdint>

/** After continuous STA link-down, force disconnect+begin (beyond SDK reconnect). */
constexpr unsigned long kWlanRecoveryLinkDownGraceMs = 45000UL;
/** Minimum spacing between forced reassociations. */
constexpr unsigned long kWlanRecoveryReassocCooldownMs = 30000UL;
/** Continuous link-down before controlled restart (OTA-blocked). */
constexpr unsigned long kWlanRecoveryRestartAfterMs = 600000UL;
/** Do not restart shortly after boot (flash/config storms). */
constexpr unsigned long kWlanRecoveryMinUptimeBeforeRestartMs = 900000UL;

enum class WlanRecoveryAction : uint8_t {
    None          = 0,
    ForcedReassoc = 1,
    Restart       = 2,
};

struct WlanRecoveryState {
    unsigned long linkDownSinceMs     = 0UL;
    unsigned long lastForcedReassocMs = 0UL;
};

/**
 * Pure recovery decision for unit tests.
 * Tracks continuous STA-down time; never reboots during OTA or without credentials.
 */
inline WlanRecoveryAction wlanRecoveryDecide(bool apMode, bool staConnectedOk, bool otaBlocking,
                                             bool hasStaCredentials, unsigned long nowMs,
                                             unsigned long uptimeMs, WlanRecoveryState& st) {
    if (apMode || !hasStaCredentials) {
        st.linkDownSinceMs = 0UL;
        return WlanRecoveryAction::None;
    }
    if (staConnectedOk) {
        st.linkDownSinceMs = 0UL;
        return WlanRecoveryAction::None;
    }

    if (st.linkDownSinceMs == 0UL) {
        st.linkDownSinceMs = (nowMs == 0UL) ? 1UL : nowMs;
        return WlanRecoveryAction::None;
    }

    // Still track outage duration during OTA, but never touch the radio/reboot.
    if (otaBlocking) {
        return WlanRecoveryAction::None;
    }

    const unsigned long downFor = nowMs - st.linkDownSinceMs;

    if (uptimeMs >= kWlanRecoveryMinUptimeBeforeRestartMs
        && downFor >= kWlanRecoveryRestartAfterMs) {
        return WlanRecoveryAction::Restart;
    }

    if (downFor >= kWlanRecoveryLinkDownGraceMs) {
        if (st.lastForcedReassocMs == 0UL
            || (nowMs - st.lastForcedReassocMs) >= kWlanRecoveryReassocCooldownMs) {
            st.lastForcedReassocMs = (nowMs == 0UL) ? 1UL : nowMs;
            return WlanRecoveryAction::ForcedReassoc;
        }
    }

    return WlanRecoveryAction::None;
}
