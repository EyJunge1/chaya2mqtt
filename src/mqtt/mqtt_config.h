#pragma once

#include <cstddef>
#include <cstdint>

constexpr const char kMqttDefaultTopicPub[] = "chaya/to_b";
constexpr const char kMqttDefaultTopicSub[] = "chaya/to_a";

/** Prefix for auto-generated MQTT pair topics: chaya/<device_id>. */
constexpr const char kMqttPairTopicPrefix[] = "chaya/";

constexpr uint16_t kMqttDefaultTlsPort = 8883;

constexpr int kMqttKeepAliveSeconds = 60;
constexpr int kMqttOutboxLimitBytes = 4096;

/** Clamp MQTT port from integer form (e.g. HTML/API); invalid uses TLS default. */
inline constexpr uint16_t normalizeMqttPort(int p) {
    return (p > 0 && p <= 65535) ? static_cast<uint16_t>(p) : kMqttDefaultTlsPort;
}
