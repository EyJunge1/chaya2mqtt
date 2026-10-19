#include "chaya_epd4c.hpp"

ChayaEpd4c::ChayaEpd4c(int16_t cs, int16_t dc, int16_t rst, int16_t busy)
    : Adafruit_GFX(WIDTH, HEIGHT), panel_(cs, dc, rst, busy) {
    setFullWindow();
}

void ChayaEpd4c::init(uint32_t, bool, uint16_t resetDurationMs, bool) {
    panel_.begin(resetDurationMs, /*useFastUpdate=*/true);
    setFullWindow();
}

void ChayaEpd4c::setBusyCallback(Jd79660Hw::BusyCallback cb, const void *param) { panel_.hw().setBusyCallback(cb, param); }

void ChayaEpd4c::setFullWindow() {
    // Full-screen only; kept for API parity with draw.cpp.
}

void ChayaEpd4c::fillScreen(uint16_t color) {
    const uint8_t packed = static_cast<uint8_t>(color4(color) * 0x55);
    for (uint32_t i = 0; i < sizeof(buffer_); ++i) {
        buffer_[i] = packed;
    }
}

void ChayaEpd4c::drawPixel(int16_t x, int16_t y, uint16_t color) {
    if ((x < 0) || (x >= width()) || (y < 0) || (y >= height())) {
        return;
    }
    switch (getRotation()) {
    case 1: {
        const int16_t t = x;
        x = WIDTH - y - 1;
        y = t;
        break;
    }
    case 2:
        x = WIDTH - x - 1;
        y = HEIGHT - y - 1;
        break;
    case 3: {
        const int16_t t = x;
        x = y;
        y = HEIGHT - t - 1;
        break;
    }
    default:
        break;
    }
    if ((x < 0) || (x >= static_cast<int16_t>(WIDTH)) || (y < 0) || (y >= static_cast<int16_t>(HEIGHT))) {
        return;
    }
    const uint32_t i = static_cast<uint32_t>(x / 4) + static_cast<uint32_t>(y) * (WIDTH / 4);
    const uint8_t pv = color4(color);
    switch (x % 4) {
    case 0:
        buffer_[i] = (buffer_[i] & 0x3F) | (pv << 6);
        break;
    case 1:
        buffer_[i] = (buffer_[i] & 0xCF) | (pv << 4);
        break;
    case 2:
        buffer_[i] = (buffer_[i] & 0xF3) | (pv << 2);
        break;
    case 3:
        buffer_[i] = (buffer_[i] & 0xFC) | pv;
        break;
    }
}

void ChayaEpd4c::firstPage() { fillScreen(EPD_WHITE); }

bool ChayaEpd4c::nextPage() {
    panel_.writeFramebuffer(buffer_, sizeof(buffer_));
    panel_.refresh();
    panel_.powerOff();
    return false;
}

void ChayaEpd4c::hibernate() { panel_.hibernate(); }

void ChayaEpd4c::powerOff() { panel_.powerOff(); }

uint8_t ChayaEpd4c::color4(uint16_t color) {
    switch (color) {
    case EPD_BLACK:
        return EPD_NATIVE_BLACK;
    case EPD_WHITE:
        return EPD_NATIVE_WHITE;
    case EPD_YELLOW:
        return EPD_NATIVE_YELLOW;
    case EPD_RED:
        return EPD_NATIVE_RED;
    default: {
        const uint16_t red = color & 0xF800;
        const uint16_t green = static_cast<uint16_t>((color & 0x07E0) << 5);
        const uint16_t blue = static_cast<uint16_t>((color & 0x001F) << 11);
        if ((red < 0x8000) && (green < 0x8000) && (blue < 0x8000)) {
            return EPD_NATIVE_BLACK;
        }
        if ((red >= 0x8000) && (green >= 0x8000) && (blue >= 0x8000)) {
            return EPD_NATIVE_WHITE;
        }
        if ((red >= 0x8000) && (green >= 0xC000)) {
            return EPD_NATIVE_YELLOW;
        }
        if (red >= 0x8000) {
            return EPD_NATIVE_RED;
        }
        return EPD_NATIVE_BLACK;
    }
    }
}
