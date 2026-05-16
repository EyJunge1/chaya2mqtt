// Trimmed SSD1682 / GDEH0154Z90 (200x200 BWR) driver for chaya2mqtt.
// Merged from vendor GxEPD2 (GxEPD2_EPD + GxEPD2_154_Z90c + GxEPD2_3C paging).

#pragma once

#include "epd_colors.h"

#include <Adafruit_GFX.h>
#include <Arduino.h>
#include <SPI.h>

class EpdDriver154Z90c : public Adafruit_GFX {
public:
    static constexpr uint16_t kWidth         = 200;
    static constexpr uint16_t kHeight      = 200;
    static constexpr uint16_t kPageHeight  = kHeight;
    static constexpr uint16_t kPowerOnMs   = 100;
    static constexpr uint16_t kPowerOffMs  = 250;
    static constexpr uint16_t kFullRefreshMs    = 14000;
    static constexpr uint16_t kPartialRefreshMs = 14000;

    explicit EpdDriver154Z90c(int16_t cs, int16_t dc, int16_t rst, int16_t busy);

    void init(uint32_t serial_diag_bitrate = 0);
    void init(uint32_t serial_diag_bitrate, bool initial, uint16_t reset_duration = 20,
              bool pulldown_rst_mode = false);
    void end();

    void setFullWindow();

    void firstPage();

    /** Full-window update path only; returns false when the frame is complete. */
    bool nextPage();

    /** Fill the current page buffers (black + color planes), not the base-class fillRect path. */
    void fillScreen(uint16_t color);

    void drawPixel(int16_t x, int16_t y, uint16_t color) override;

    void hibernate();

private:
    void powerOff();

    void writeScreenBuffer(uint8_t black_value, uint8_t color_value);
    void writeImage(const uint8_t* black, const uint8_t* color, int16_t x, int16_t y, int16_t w,
                    int16_t h, bool invert, bool mirror_y, bool pgm);

    void refresh(bool partial_update_mode);
    void refresh(int16_t x, int16_t y, int16_t w, int16_t h);

    void _reset();
    void _waitWhileBusy(const char* comment, uint16_t busy_time);
    void _writeCommand(uint8_t c);
    void _writeData(uint8_t d);

    void _setPartialRamArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
    void _powerOffEp();
    void _initDisplay();
    void _initPart();
    void _updateFull();
    void _updatePart();

    template <typename T>
    static void swapVar(T& a, T& b) {
        T t = a;
        a   = b;
        b   = t;
    }
    static uint16_t minU16(uint16_t a, uint16_t b) { return (a < b ? a : b); }

    static constexpr int16_t kBusyLevel   = HIGH;
    static constexpr uint32_t kBusyTimeoutUs = 20000000;

    int16_t _cs = -1;
    int16_t _dc = -1;
    int16_t _rst = -1;
    int16_t _busy = -1;
    uint32_t _busy_timeout = kBusyTimeoutUs;
    bool _diag_enabled = false;
    bool _pulldown_rst_mode = false;
    SPIClass* _p_spi = &SPI;
    SPISettings _spi_settings{4000000, MSBFIRST, SPI_MODE0};

    bool _initial_write = true;
    bool _power_is_on = false;
    bool _hibernating = false;
    uint16_t _reset_duration = 10;

    bool _gfx_partial_window = false;
    int16_t _current_page = 0;
    uint16_t _pages = 0;
    uint16_t _page_height = kPageHeight;
    uint16_t _pw_x = 0;
    uint16_t _pw_y = 0;
    uint16_t _pw_w = 0;
    uint16_t _pw_h = 0;

    uint8_t _black_buffer[(kWidth / 8) * kPageHeight]{};
    uint8_t _color_buffer[(kWidth / 8) * kPageHeight]{};
};
