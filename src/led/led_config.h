#pragma once

#include "hw/pins.h"

#include <cstdint>

static constexpr int kButtonLedPin = pins::kButtonLed;

constexpr unsigned long kFailFlashMs = 50;

constexpr unsigned kLedSequenceStepMs = 100;
constexpr unsigned kPostPublishWaitMs = 500;

constexpr unsigned long kLedRefreshPulseMs = 280UL;
constexpr unsigned long kLedRefreshAckMs = 1500UL;

/** Status / preset blink timings (green header LED). */
constexpr uint8_t kLedPresetBootCount = 3;
constexpr uint16_t kLedPresetBootOnMs = 200;
constexpr uint16_t kLedPresetBootOffMs = 200;
constexpr uint8_t kLedPresetWifiUpCount = 2;
constexpr uint16_t kLedPresetWifiUpOnMs = 80;
constexpr uint16_t kLedPresetWifiUpOffMs = 80;
constexpr uint8_t kLedPresetMqttUpCount = 1;
constexpr uint16_t kLedPresetMqttUpOnMs = 150;
constexpr uint16_t kLedPresetMqttUpOffMs = 0;
constexpr uint8_t kLedPresetLinkDownCount = 4;
constexpr uint16_t kLedPresetLinkDownOnMs = 50;
constexpr uint16_t kLedPresetLinkDownOffMs = 50;
/** Soft-off armed ack (held ≥2 s; shutdown starts on release). */
constexpr uint8_t kLedPresetSoftOffCount = 3;
constexpr uint16_t kLedPresetSoftOffOnMs = 60;
constexpr uint16_t kLedPresetSoftOffOffMs = 60;
