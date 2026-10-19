#include "internal.hpp"

#include "hw/pins.hpp"
#include "util/log_tag.hpp"

#include <Arduino.h>
#include <SPI.h>
#include <cstdint>
#include <esp_log.h>

DEFINE_LOG_TAG("DISP");

static ChayaEpdPanel display(/*CS=*/pins::kSpiCs, /*DC=*/pins::kDisplayDc, /*RST=*/pins::kDisplayRst,
                             /*BUSY=*/pins::kDisplayBusy);

static bool s_panelInited = false;
static bool s_hibernating = false;
static uint32_t s_busyCbLastLogMs = 0;

ChayaEpdPanel &displayPanel() { return display; }

static void displayBusyProgressCb(const void *) {
    const uint32_t now = millis();
    if (now - s_busyCbLastLogMs >= 2000U) {
        s_busyCbLastLogMs = now;
        ESP_LOGI(TAG, "EPD busy… pin=%d pwr_en=%d", digitalRead(pins::kDisplayBusy), digitalRead(pins::kDisplayPwrEn));
    }
    // Yield while waiting on BUSY so the task WDT stays happy during long refreshes.
    delay(1);
}

static void displaySetPanelPower(bool on) {
    // Waveshare: EPD_PWR LOW = on.
    pinMode(pins::kDisplayPwrEn, OUTPUT);
    digitalWrite(pins::kDisplayPwrEn, on ? LOW : HIGH);
}

static void displayAttachSpiPins() {
    // Must run before panel init. SPI.begin() without pins uses ESP32-S3 defaults
    // (MOSI=11, MISO=13) which swap CS and SDI. begin() is a no-op if already started.
    SPI.begin(/*SCK=*/pins::kSpiSck, /*MISO=*/pins::kSpiMiso, /*MOSI=*/pins::kSpiMosi,
              /*SS=*/pins::kSpiCs);
}

void displayInitEpd() {
    static constexpr uint16_t kEpdResetDurationMs = 2;

    displayAttachSpiPins();
    display.init(0, true, kEpdResetDurationMs, false);
    displayAttachSpiPins();
    pinMode(pins::kDisplayBusy, INPUT);
    display.setBusyCallback(displayBusyProgressCb, nullptr);
    s_busyCbLastLogMs = 0;
    s_panelInited = true;
    s_hibernating = false;
    ESP_LOGI(TAG, "EPD ready busy=%d", digitalRead(pins::kDisplayBusy));
}

void displayHwInitPins() {
    displaySetPanelPower(true);
    delay(10); // Waveshare ESP-IDF epaper_power_up
    pinMode(pins::kDisplayBusy, INPUT);
    pinMode(pins::kDisplayRst, OUTPUT);
    pinMode(pins::kDisplayDc, OUTPUT);
    pinMode(pins::kSpiSck, OUTPUT);
    pinMode(pins::kSpiMosi, OUTPUT);
    pinMode(pins::kSpiCs, OUTPUT);
    digitalWrite(pins::kSpiCs, HIGH);
    digitalWrite(pins::kSpiSck, LOW);
    digitalWrite(pins::kDisplayRst, HIGH);
}

void displayResumeSpiForDraw() {
    // Keep EPD3V3_EN on; never SPI.end() or rail-cycle between frames.
    // Hibernate wakes with init() + RST only.
    if (!s_hibernating && s_panelInited) {
        return;
    }
    displayInitEpd();
}

void displaySuspendSpiLowPower() {
    display.setBusyCallback(nullptr, nullptr);
    s_hibernating = true;
}
