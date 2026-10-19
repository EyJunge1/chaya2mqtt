#pragma once

/** Soft-off may latch-cut only when no OTA, shutdown, factory wipe, or in-flight admin apply owns the device. */
inline auto softOffAllowed(bool otaBusy, bool shutdown, bool factoryQueued, bool applyInFlight = false,
                           bool mqttApplyPending = false, bool settingsApplyPending = false) -> bool {
    return !otaBusy && !shutdown && !factoryQueued && !applyInFlight && !mqttApplyPending && !settingsApplyPending;
}

/** Factory POST is busy when shutdown/factory already own, apply/OTA block, or a restart is armed. */
inline auto factoryResetHttpBlocked(bool shutdown, bool factoryQueued, bool restartBlocked, bool rebootRequested,
                                    bool wifiReconnectRequested) -> bool {
    return shutdown || factoryQueued || restartBlocked || rebootRequested || wifiReconnectRequested;
}
