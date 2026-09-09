#pragma once

#include <cstdint>

/** True when admin reboot / Wi-Fi-save restart must wait (OTA, RAM apply, or in-flight POST). */
inline auto webAdminRestartBlocked(bool otaBusy, bool mqttApplyPending, bool settingsApplyPending,
                                   bool mqttApplyUnqueued, bool applyInFlight) -> bool {
    return otaBusy || mqttApplyPending || settingsApplyPending || mqttApplyUnqueued || applyInFlight;
}

/** MQTT/Settings POST may commit RAM-pending only while still armed and not shutting down. */
inline auto webAdminApplyCommitAllowed(bool shutdown, bool armed, bool factoryQueued = false) -> bool {
    return armed && !shutdown && !factoryQueued;
}

/** Settings/MQTT apply and STA scan wait while OTA owns the radio (BUG-WEB-01 / BUG-NET-04). */
inline auto adminApplyBlockedByOta(bool otaBusy) -> bool { return otaBusy; }

/** OTA check/install waits for Settings/MQTT apply — RAM pending dies on reboot. */
inline auto webAdminOtaStartBlocked(bool mqttApplyPending, bool settingsApplyPending, bool mqttApplyUnqueued,
                                    bool applyInFlight) -> bool {
    return webAdminRestartBlocked(false, mqttApplyPending, settingsApplyPending, mqttApplyUnqueued, applyInFlight);
}

/** Deferred Settings/MQTT apply may run only when neither shutdown, OTA, nor factory owns the device. */
inline auto webAdminDeferredApplyAllowed(bool shutdown, bool otaBusy, bool factoryQueued = false) -> bool {
    return !shutdown && !otaBusy && !factoryQueued;
}

/** RC-WEB-15: MQTT apply is unqueued when version is strictly ahead of the last queued NetCmd. */
inline auto webAdminMqttApplyUnqueuedPure(uint32_t version, uint32_t queued) -> bool {
    return version > queued;
}
