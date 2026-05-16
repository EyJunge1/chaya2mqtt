// EPD RGB565 color literals (same values as upstream GxEPD2 for compatibility).

#pragma once

// Native names
#define EPD_BLACK 0x0000
#define EPD_WHITE 0xFFFF
#define EPD_RED 0xF800

// Compatibility with existing draw code (GxEPD2-style names)
#define GxEPD_BLACK EPD_BLACK
#define GxEPD_WHITE EPD_WHITE
#define GxEPD_RED EPD_RED
