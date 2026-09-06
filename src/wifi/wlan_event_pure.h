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
