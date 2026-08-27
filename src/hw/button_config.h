#pragma once

#include "pins.h"

#include <cstdint>

static constexpr int kButtonLedPin = pins::kButtonLed;

constexpr unsigned long kDebounceStableMs = 20;
constexpr unsigned long kShortPressMinMs           = 50;
constexpr unsigned long kFailFlashMs               = 50;

constexpr unsigned kLedSequenceStepMs      = 100;
constexpr unsigned kPostPublishWaitMs      = 500;

constexpr unsigned long kButtonTaskPollActiveMs  = 10UL;
constexpr unsigned long kButtonTaskPollIdleMs    = 50UL;
constexpr unsigned long kButtonStartupBlinkMs    = 200UL;
constexpr int           kButtonStartupBlinkCount = 3;

constexpr unsigned long kSoftOffHoldMs             = 2000UL;
constexpr uint32_t      kPowerOffDisplayTimeoutMs  = 90000U;
constexpr unsigned long kLedRefreshPulseMs         = 280UL;
constexpr unsigned long kLedRefreshAckMs           = 1500UL;
