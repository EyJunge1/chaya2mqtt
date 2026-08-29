#pragma once

#include <cstdint>

constexpr unsigned long kDebounceStableMs = 20;
constexpr unsigned long kShortPressMinMs  = 50;

constexpr unsigned long kButtonTaskPollActiveMs = 10UL;
constexpr unsigned long kButtonTaskPollIdleMs   = 50UL;

constexpr unsigned long kSoftOffHoldMs          = 2000UL;
/** Stable PWR HIGH required before arming level-triggered EXT1 wake (bounce settle). */
constexpr unsigned long kSoftOffReleaseSettleMs = 300UL;
/** Max wait for PWR release before sleep anyway (stuck button / STAB-05). */
constexpr unsigned long kSoftOffReleaseTimeoutMs = 15000UL;
constexpr uint32_t      kPowerOffDisplayTimeoutMs = 90000U;
