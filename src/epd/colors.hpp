#pragma once

#include <cstdint>

/** RGB565-style constants for Adafruit_GFX drawing on the 4-color panel. */
inline constexpr uint16_t EPD_BLACK = 0x0000;
inline constexpr uint16_t EPD_WHITE = 0xFFFF;
inline constexpr uint16_t EPD_RED = 0xF800;
inline constexpr uint16_t EPD_YELLOW = 0xFFE0;

/** Native 2-bit panel encoding (MSB-first, four pixels per byte). */
inline constexpr uint8_t EPD_NATIVE_BLACK = 0x0;
inline constexpr uint8_t EPD_NATIVE_WHITE = 0x1;
inline constexpr uint8_t EPD_NATIVE_YELLOW = 0x2;
inline constexpr uint8_t EPD_NATIVE_RED = 0x3;
