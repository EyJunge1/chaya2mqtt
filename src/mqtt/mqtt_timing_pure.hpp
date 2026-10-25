#pragma once

/** Backoff tuning (no broker / WiFi down / TLS vs NTP) — free of FreeRTOS types. */
constexpr unsigned long kMqttBrokerMissingBackoffMs = 60000UL;
constexpr unsigned long kMqttBackoffInitialMs = 30000UL;
constexpr unsigned long kMqttBackoffMaxMs = 60000UL;
constexpr unsigned long kMqttWifiDownBackoffMs = 20000UL;
constexpr unsigned long kMqttWifiLostDuringTlsBackoffMs = 90000UL;
constexpr unsigned long kMqttNtpRetryMs = 2000UL;

constexpr unsigned long kMqttPublishAckWaitMs = 5000UL;

constexpr int kMqttWifiNetworkTimeoutMs = 5000;
constexpr int kMqttReconnectTimeoutMs = 60000;
