#pragma once

#include "pins.h"

static constexpr int kButtonLedPin = pins::kButtonLed;

constexpr unsigned long kDebounceStableMs = 20;
constexpr unsigned long kFactoryResetHoldMs = 10000;
constexpr unsigned long kShortPressMinMs           = 50;
constexpr unsigned kPublishMaxAttempts             = 2;
constexpr unsigned long kPublishRetryDelayMs     = 25;
constexpr unsigned long kFailFlashMs               = 50;

constexpr unsigned kLedSequenceStepMs      = 100;
constexpr unsigned kPostPublishWaitMs      = 500;
constexpr unsigned kAuthBlinkHalfPeriodMs = 500;
constexpr int      kFactoryResetLedBlinkCycles = 6;
constexpr unsigned long kFactoryResetLedBlinkPeriodMs = 120;

constexpr unsigned long kButtonTaskPollActiveMs  = 10UL;
constexpr unsigned long kButtonTaskPollIdleMs    = 50UL;
constexpr unsigned long kButtonStartupBlinkMs    = 200UL;
constexpr int           kButtonStartupBlinkCount = 3;
