#pragma once

#include <cstdint>

enum class WlanForceReassocResult : uint8_t { Begun = 0, SkippedConnected = 1, Deferred = 2 };

/**
 * Pure Soft→Force decision: after @p softAttemptsBeforeForce soft reconnects,
 * escalate to forced reassociation (TEST-01 / STAB recovery).
 */
inline auto wlanSoftReconnectShouldForce(uint32_t failCount, uint32_t softAttemptsBeforeForce) -> bool {
    return failCount >= softAttemptsBeforeForce;
}

/** True when force was deferred — restore pending / undo cooldown. */
inline auto wlanForceCallerShouldUndo(WlanForceReassocResult result) -> bool {
    return result == WlanForceReassocResult::Deferred;
}

/** Increment fail-count only when force actually began (RC-NET-10). */
inline auto wlanForceCallerShouldCountFail(WlanForceReassocResult result) -> bool {
    return result == WlanForceReassocResult::Begun;
}
