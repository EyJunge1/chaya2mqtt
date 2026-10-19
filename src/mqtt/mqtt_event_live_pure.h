#pragma once

#include <cstdint>

#include "heart/counter_pure.h"

/** Event belongs to the live client when the handle matches and the generation has not bumped. */
inline auto mqttEventIsLive(const void *evClient, const void *liveClient, uint32_t genSnap, uint32_t genNow) -> bool {
    return evClient != nullptr && evClient == liveClient && genSnap == genNow;
}

/**
 * Own-topic heart payload: ignore self-echo while a publish is in flight, otherwise
 * apply only when the incoming absolute count is strictly greater than local TX.
 */
inline auto mqttOwnTopicTxShouldApply(int incoming, int localTx, bool publishInFlight, int pendingExpected) -> bool {
    if (publishInFlight && incoming <= pendingExpected) {
        return false;
    }
    return heartSentRemoteShouldApply(incoming, localTx);
}
