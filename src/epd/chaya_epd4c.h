#pragma once

#include "colors.h"
#include "jd79660_panel.h"

#include <Adafruit_GFX.h>
#include <cstdint>

/**
 * Full-screen Adafruit_GFX front-end for the 200x200 4-color JD79660 panel.
 * Page height equals panel height → single firstPage/nextPage cycle.
 */
class ChayaEpd4c : public Adafruit_GFX {
  public:
    static constexpr uint16_t WIDTH = Jd79660Panel::kWidth;
    static constexpr uint16_t HEIGHT = Jd79660Panel::kHeight;

    ChayaEpd4c(int16_t cs, int16_t dc, int16_t rst, int16_t busy);

    void init(uint32_t /*serialDiagBitrate*/ = 0, bool /*initial*/ = true, uint16_t resetDurationMs = 2,
              bool /*pulldownRst*/ = false);

    void setBusyCallback(Jd79660Hw::BusyCallback cb, const void *param = nullptr);

    void drawPixel(int16_t x, int16_t y, uint16_t color) override;
    void fillScreen(uint16_t color) override;

    void setFullWindow();
    void firstPage();
    bool nextPage();

    void hibernate();
    void powerOff();

    /** Panel access for smoke tests (fillNative / writeFramebuffer). */
    Jd79660Panel &panel() { return panel_; }

  private:
    static uint8_t color4(uint16_t color);

    Jd79660Panel panel_;
    uint8_t buffer_[Jd79660Panel::kFramebufferBytes]{};
};
