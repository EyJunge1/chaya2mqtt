#pragma once

#include <freertos/FreeRTOS.h>

/** Backoff tuning (no broker / WiFi down / TLS vs NTP). */
constexpr unsigned long kMqttBrokerMissingBackoffMs     = 60000UL;
constexpr unsigned long kMqttBackoffInitialMs           = 30000UL;
constexpr unsigned long kMqttBackoffMaxMs               = 60000UL;
constexpr unsigned long kMqttWifiDownBackoffMs          = 20000UL;
constexpr unsigned long kMqttWifiLostDuringTlsBackoffMs = 90000UL;
constexpr unsigned long kMqttNtpRetryMs                 = 2000UL;

constexpr TickType_t      kMqttClientLockTimeoutTicks   = pdMS_TO_TICKS(2000);
constexpr TickType_t      kChayaPublishLockTimeoutTicks = pdMS_TO_TICKS(2000);
constexpr unsigned long   kMqttBrokerRedrawMinIntervalMs = 30000UL;

constexpr int kMqttWifiNetworkTimeoutMs = 5000;
constexpr int kMqttReconnectTimeoutMs   = 60000;
