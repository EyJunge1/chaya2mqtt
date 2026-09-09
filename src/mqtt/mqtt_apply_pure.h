#pragma once

/** Clear applyPending only when no newer pending snapshot remains (RC-WEB-01). */
inline auto mqttSettingsApplyShouldClearPending(bool hasUnappliedPending) -> bool { return !hasUnappliedPending; }

/** Kill timeout: keep apply pending and publish blocked (RC-MQTT-11). */
inline auto mqttSettingsApplyShouldFinish(bool killOk) -> bool { return killOk; }

/** Skip NVS/disconnect work while EPD, factory wipe, or shutdown owns the device. */
inline auto mqttSettingsApplyShouldDefer(bool epdActive, bool factoryQueued, bool shutdown) -> bool {
    return epdActive || factoryQueued || shutdown;
}

/** Apply-to-active already ran, but teardown/setup is still incomplete (RC-MQTT-11). */
inline auto mqttSettingsApplyNothingPendingNeedsRetry(bool killCoalescePending) -> bool {
    return killCoalescePending;
}
