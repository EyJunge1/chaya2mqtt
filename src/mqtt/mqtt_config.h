#pragma once

#include <cstdint>

/** Prefix for auto-generated MQTT pair topics: chaya2mqtt/<device_id>. */
constexpr const char kMqttPairTopicPrefix[] = "chaya2mqtt/";

constexpr uint16_t kMqttDefaultTlsPort   = 8883;
constexpr uint16_t kMqttDefaultPlainPort = 1883;

constexpr int kMqttKeepAliveSeconds = 60;
constexpr int kMqttOutboxLimitBytes = 4096;

/** Clamp MQTT port from integer form (e.g. HTML/API); invalid uses TLS default. */
inline constexpr uint16_t normalizeMqttPort(int p) {
    return (p > 0 && p <= 65535) ? static_cast<uint16_t>(p) : kMqttDefaultTlsPort;
}

/** Standard port for the given transport (mqtt vs mqtts). */
inline constexpr uint16_t mqttDefaultPortForTls(bool tls) {
    return tls ? kMqttDefaultTlsPort : kMqttDefaultPlainPort;
}
