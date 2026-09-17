#pragma once

#include <cstdint>

/** Event belongs to the live client when the handle matches and the generation has not bumped. */
inline auto mqttEventIsLive(const void *evClient, const void *liveClient, uint32_t genSnap, uint32_t genNow) -> bool {
    return evClient != nullptr && evClient == liveClient && genSnap == genNow;
}
