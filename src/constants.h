#pragma once

#include <cstdint>
#include <ctime>

/** Minimum plausible Unix time (UTC) after NTP sync — rejects unset RTC (~1970). */
constexpr uint32_t kNtpMinValidUtcEpoch = 1700000000U;

constexpr const char kMqttDefaultTopicPub[] = "chaya/to_b";
constexpr const char kMqttDefaultTopicSub[] = "chaya/to_a";

constexpr uint16_t kMqttDefaultTlsPort = 8883;

/** Clamp MQTT port from integer form (e.g. HTML/API); invalid uses TLS default. */
inline constexpr uint16_t normalizeMqttPort(int p) {
    return (p > 0 && p <= 65535) ? static_cast<uint16_t>(p) : kMqttDefaultTlsPort;
}

inline bool ntpTimeLooksSynced(time_t utcNow) {
    return utcNow > static_cast<time_t>(kNtpMinValidUtcEpoch);
}
