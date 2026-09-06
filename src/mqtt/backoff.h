#pragma once

#include <algorithm>

#include "mqtt_timing_pure.h"

struct MqttBackoffState {
    unsigned long lastAttemptAtMs = 0;
    unsigned long backoffPeriodMs = 0;
    unsigned long currentBackoffMs = kMqttBackoffInitialMs;
};

/** Compute next wait after a disconnect failure; updates currentBackoffMs. */
inline auto mqttNextFailureBackoffMs(MqttBackoffState &st, bool wifiSuspect) -> unsigned long {
    unsigned long waitMs = st.currentBackoffMs;
    if (wifiSuspect) {
        waitMs = std::max(waitMs, kMqttWifiLostDuringTlsBackoffMs);
    }
    st.currentBackoffMs = std::min(st.currentBackoffMs * 2UL, kMqttBackoffMaxMs);
    st.backoffPeriodMs = waitMs;
    return waitMs;
}

/** Reset backoff after a successful connection. */
inline void mqttBackoffResetOnConnect(MqttBackoffState &st) {
    st.lastAttemptAtMs = 0;
    st.backoffPeriodMs = 0;
    st.currentBackoffMs = kMqttBackoffInitialMs;
}

/** True when enough time has elapsed since lastAttemptAtMs for backoffPeriodMs. */
inline auto mqttBackoffElapsed(const MqttBackoffState &st, unsigned long nowMs) -> bool {
    if (st.backoffPeriodMs == 0UL) {
        return true;
    }
    return (nowMs - st.lastAttemptAtMs) >= st.backoffPeriodMs;
}

/**
 * Precheck deferral before starting a connect attempt.
 * Returns 0 when connect may proceed; otherwise a deferral period in ms.
 */
inline auto mqttConnectPrecheckDeferMsPure(bool brokerConfigured, bool wifiConnected, bool wifiStable,
                                           bool ntpSynced) -> unsigned long {
    if (!brokerConfigured) {
        return kMqttBrokerMissingBackoffMs;
    }
    if (!wifiConnected) {
        return kMqttWifiDownBackoffMs;
    }
    if (!wifiStable || !ntpSynced) {
        return kMqttNtpRetryMs;
    }
    return 0;
}
