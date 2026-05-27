#pragma once

#include <cstdint>

/** STA stability / scan timing (module-local; device limits remain in constants.h). */
constexpr unsigned long kStaStableAfterGotIpMs   = 3000UL;
constexpr unsigned long kWifiScanKickMinIntervalMs = 20000UL;
constexpr unsigned long kWifiScanFailBackoffMs     = 5000UL;
constexpr unsigned long kWifiReconnectBaseBackoffMs = 3000UL;
constexpr unsigned long kWifiReconnectMaxBackoffMs  = 120000UL;
constexpr unsigned long kApDnsPollIntervalMs        = 5000UL;

constexpr uint32_t kWifiCredPackedMagic = 0x43575631U;
