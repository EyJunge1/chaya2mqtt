#pragma once

#include <freertos/FreeRTOS.h>

#include "mqtt_timing_pure.h"

constexpr TickType_t kMqttClientLockTimeoutTicks   = pdMS_TO_TICKS(2000);
constexpr TickType_t kChayaPublishLockTimeoutTicks = pdMS_TO_TICKS(2000);
