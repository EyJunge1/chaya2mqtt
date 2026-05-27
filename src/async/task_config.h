#pragma once

#include <cstdint>

/** FreeRTOS task stack sizes (bytes). */
constexpr uint32_t kNetworkTaskStackBytes = 7168U;
constexpr uint32_t kButtonTaskStackBytes  = 4096U;
constexpr uint32_t kAppTaskStackBytes     = 4096U;
constexpr uint32_t kOtaTaskStackBytes     = 8192U;
constexpr uint32_t kDisplayTaskStackBytes = 4096U;
constexpr uint32_t kMqttClientTaskStackBytes = 10240U;

/** g_netCmdQueue depth. */
constexpr uint8_t kNetCmdQueueDepth    = 32U;
constexpr uint8_t kDisplayCmdQueueDepth = 32U;
