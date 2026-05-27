#pragma once

#include <cstddef>
#include <cstdint>

/** Wi-Fi SSID/password buffers (IEEE max + NUL). */
constexpr size_t kWifiSsidMaxLen = 33U;
constexpr size_t kWifiPassMaxLen = 65U;

constexpr unsigned long kWifiStaBootConnectTimeoutMs = 5000UL;
constexpr uint8_t       kWifiStaMaxTxPowerQuarterDbm = 52U;
constexpr uint16_t      kWifiStaInactiveTimeSeconds  = 30U;

/** STA stability / scan timing (module-local). */
constexpr unsigned long kStaStableAfterGotIpMs   = 3000UL;
constexpr unsigned long kWifiScanKickMinIntervalMs = 20000UL;
constexpr unsigned long kWifiScanFailBackoffMs     = 5000UL;
constexpr unsigned long kWifiReconnectBaseBackoffMs = 3000UL;
constexpr unsigned long kWifiReconnectMaxBackoffMs  = 120000UL;
constexpr unsigned long kApDnsPollIntervalMs        = 5000UL;

constexpr uint32_t kWifiCredPackedMagic = 0x43575631U;
