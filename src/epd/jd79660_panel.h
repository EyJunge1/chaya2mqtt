#pragma once

#include "jd79660_hw.h"

#include <cstdint>

/**
 * GDEM0154F51H (JD79660) panel control: init, framebuffer write, refresh, sleep.
 * Command bytes match the Waveshare 1.54G demo; fast-mode cmds E0/E6/A5 run
 * before power-on (0x04).
 */
class Jd79660Panel {
  public:
    static constexpr uint16_t kWidth = 200;
    static constexpr uint16_t kHeight = 200;
    static constexpr uint32_t kFramebufferBytes = (kWidth * kHeight) / 4; // 2 bpp

    Jd79660Panel(int16_t cs, int16_t dc, int16_t rst, int16_t busy);

    Jd79660Hw &hw() { return hw_; }
    const Jd79660Hw &hw() const { return hw_; }

    /** Configure GPIO and run panel init (fast waveform by default). Single reset. */
    void begin(uint16_t resetDurationMs = 2, bool useFastUpdate = true);

    /** Re-init after deep sleep / hibernate (pins already configured). */
    void wake(uint16_t resetDurationMs = 2, bool useFastUpdate = true);

    void ensureInit();
    void writeFramebuffer(const uint8_t *data, uint32_t len = kFramebufferBytes);
    /** Fill controller RAM with a packed native color (0..3) and refresh. */
    void fillNative(uint8_t nativeColor);
    void refresh();
    void powerOff();
    void hibernate();

  private:
    void initDisplay(bool useFastUpdate);
    void powerOn();
    /** Write full controller RAM (cmd 0x10) without refresh. */
    void writeControllerFill(uint8_t packedByte);

    Jd79660Hw hw_;
    uint16_t resetDurationMs_ = 2;
    bool useFastUpdate_ = true;
    bool powerOn_ = false;
    bool hibernating_ = false;
    bool initDone_ = false;
    bool needsHardReset_ = true;
    /** Clear controller RAM once before the first image write. */
    bool needsInitialRamClear_ = true;
};
