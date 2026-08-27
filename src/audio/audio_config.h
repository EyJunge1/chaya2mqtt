#pragma once

#include <cstdint>

constexpr uint8_t kEs8311I2cAddr      = 0x18;
constexpr uint8_t kAudioDefaultVolume = 70;
constexpr uint8_t kAudioDefaultQuiet0 = 23;
constexpr uint8_t kAudioDefaultQuiet1 = 8;
constexpr uint32_t kAudioSampleRateHz = 16000;
constexpr uint8_t kAudioVolumeMax     = 100;
constexpr uint8_t kAudioHourMax       = 23;

/** Configurable sine click (TX/RX): Hz and duration in ms. */
constexpr uint16_t kAudioToneHzMin     = 40;
constexpr uint16_t kAudioToneHzMax     = 2000;
constexpr uint16_t kAudioToneMsMin     = 20;
constexpr uint16_t kAudioToneMsMax     = 500;
constexpr uint16_t kAudioDefaultTxHz   = 95;
constexpr uint16_t kAudioDefaultTxMs   = 80;
constexpr uint16_t kAudioDefaultRxHz   = 88;
constexpr uint16_t kAudioDefaultRxMs   = 140;
