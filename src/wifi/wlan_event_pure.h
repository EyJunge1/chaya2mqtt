#pragma once

#include <cstdint>

/**
 * After pending.exchange(true): enqueue a NetCmd only on the false→true edge.
 * Already-true means a command is in-flight or wlanLoop will drain the flag.
 */
inline auto wlanNetCmdShouldEnqueue(bool previousPending) -> bool { return !previousPending; }

/**
 * Wrap-safe "nowMs is still before nextAllowedMs" (same signed check as STA reconnect).
 * nextAllowedMs == 0 means no deadline (due now).
 */
inline auto wlanMsBeforeDeadline(unsigned long nowMs, unsigned long nextAllowedMs) -> bool {
    return nextAllowedMs != 0UL && static_cast<std::int32_t>(nowMs - nextAllowedMs) < 0;
}

/** Kick stays queued while a connection test or OTA owns the radio (BUG-NET-04). */
inline auto wifiScanServiceShouldDeferKick(bool connectionTestOwnsRadio, bool otaBusy) -> bool {
    return connectionTestOwnsRadio || otaBusy;
}

/** User refresh always sets the kick; the service coalesces in-progress scans (RC-NET-11). */
inline auto wifiScanRefreshSetsKick(bool /*inProgress*/) -> bool { return true; }

/** Do not consume the kick (or start a second sweep) while a scan is already running. */
inline auto wifiScanServiceMayStartKick(bool inProgress) -> bool { return !inProgress; }

/** Consume mDNS restart only when STA is up and the EPD window is idle (RC-NET-12 / RC-NET-14). */
inline auto wlanMdnsKickShouldConsume(bool epdActive, bool apMode, bool staOk) -> bool {
    return !epdActive && !apMode && staOk;
}
