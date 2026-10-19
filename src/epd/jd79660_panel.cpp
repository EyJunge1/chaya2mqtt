#include "jd79660_panel.h"

#include "colors.h"

#include <Arduino.h>

Jd79660Panel::Jd79660Panel(int16_t cs, int16_t dc, int16_t rst, int16_t busy)
    : hw_(cs, dc, rst, busy) {}

void Jd79660Panel::begin(uint16_t resetDurationMs, bool useFastUpdate) {
    resetDurationMs_ = resetDurationMs;
    useFastUpdate_ = useFastUpdate;
    needsHardReset_ = true;
    needsInitialRamClear_ = true;
    hw_.beginPins();
    initDisplay(useFastUpdate_);
}

void Jd79660Panel::wake(uint16_t resetDurationMs, bool useFastUpdate) {
    resetDurationMs_ = resetDurationMs;
    useFastUpdate_ = useFastUpdate;
    needsHardReset_ = true;
    needsInitialRamClear_ = true;
    hibernating_ = false;
    initDone_ = false;
    powerOn_ = false;
    initDisplay(useFastUpdate_);
}

void Jd79660Panel::ensureInit() {
    if (!initDone_) {
        initDisplay(useFastUpdate_);
    }
}

void Jd79660Panel::initDisplay(bool useFastUpdate) {
    if (needsHardReset_ || hibernating_) {
        hw_.reset(resetDurationMs_);
        hw_.waitWhileBusy();
        hibernating_ = false;
        powerOn_ = false;
        needsHardReset_ = false;
    }

    hw_.writeCommand(0x4D);
    hw_.writeData(0x78);

    hw_.writeCommand(0x00); // PSR
    hw_.writeData(0x0F);
    hw_.writeData(0x29);

    hw_.writeCommand(0x06); // BTST_P
    hw_.writeData(0x0D);
    hw_.writeData(0x12);
    hw_.writeData(0x30);
    hw_.writeData(0x20);
    hw_.writeData(0x19);
    hw_.writeData(0x2A);
    hw_.writeData(0x22);

    hw_.writeCommand(0x50); // CDI
    hw_.writeData(0x37);

    hw_.writeCommand(0x61); // TRES 200x200
    hw_.writeData(kWidth / 256);
    hw_.writeData(kWidth % 256);
    hw_.writeData(kHeight / 256);
    hw_.writeData(kHeight % 256);

    hw_.writeCommand(0xE9);
    hw_.writeData(0x01);

    hw_.writeCommand(0x30);
    hw_.writeData(0x08);

    if (useFastUpdate) {
        // Fast block before power-on.
        hw_.writeCommand(0xE0);
        hw_.writeData(0x02);
        hw_.writeCommand(0xE6);
        hw_.writeData(0x5D);
        hw_.writeCommand(0xA5);
        hw_.writeData(0x00);
        hw_.waitWhileBusy();
    }

    powerOn();
    initDone_ = true;
}

void Jd79660Panel::powerOn() {
    if (!powerOn_) {
        hw_.writeCommand(0x04);
        hw_.waitWhileBusy();
        powerOn_ = true;
    }
}

void Jd79660Panel::writeControllerFill(uint8_t packedByte) {
    hw_.writeCommand(0x10);
    hw_.writeDataRepeat(packedByte, kFramebufferBytes);
}

void Jd79660Panel::writeFramebuffer(const uint8_t *data, uint32_t len) {
    if (data == nullptr || len == 0) {
        return;
    }
    ensureInit();
    if (needsInitialRamClear_) {
        // Wipe controller RAM before the first image.
        writeControllerFill(static_cast<uint8_t>(EPD_NATIVE_WHITE * 0x55));
        needsInitialRamClear_ = false;
    }
    delay(1); // yield margin before long SPI burst
    hw_.writeCommand(0x10);
    const uint32_t n = len < kFramebufferBytes ? len : kFramebufferBytes;
    hw_.writeData(data, n);
    if (n < kFramebufferBytes) {
        hw_.writeDataRepeat(static_cast<uint8_t>(EPD_NATIVE_WHITE * 0x55), kFramebufferBytes - n);
    }
}

void Jd79660Panel::fillNative(uint8_t nativeColor) {
    const uint8_t c = nativeColor & 0x03;
    const uint8_t packed = static_cast<uint8_t>((c << 6) | (c << 4) | (c << 2) | c);
    ensureInit();
    writeControllerFill(packed);
    needsInitialRamClear_ = false;
    refresh();
}

void Jd79660Panel::refresh() {
    hw_.writeCommand(0x50); // CDI full-refresh
    hw_.writeData(0x37);
    hw_.writeCommand(0x12); // DISPLAY_REFRESH
    hw_.writeData(0x00);
    delay(1);
    hw_.waitWhileBusy();
    // Controller needs a fresh init before the next RAM write.
    initDone_ = false;
}

void Jd79660Panel::powerOff() {
    if (powerOn_) {
        hw_.writeCommand(0x02);
        hw_.writeData(0x00);
        hw_.waitWhileBusy();
        powerOn_ = false;
    }
}

void Jd79660Panel::hibernate() {
    powerOff();
    hw_.writeCommand(0x07); // DEEP_SLEEP
    hw_.writeData(0xA5);
    hibernating_ = true;
    initDone_ = false;
    needsHardReset_ = true;
    needsInitialRamClear_ = true;
}
