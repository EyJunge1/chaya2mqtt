#pragma once

#include <cstdint>

/**
 * Pure Soft→Force decision: after @p softAttemptsBeforeForce soft reconnects,
 * escalate to forced reassociation (TEST-01 / STAB recovery).
 */
inline auto wlanSoftReconnectShouldForce(uint32_t failCount, uint32_t softAttemptsBeforeForce) -> bool {
    return failCount >= softAttemptsBeforeForce;
}
