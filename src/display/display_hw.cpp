#include "internal.h"

#include "hw/pins.h"
#include "util/log_tag.h"

#include <Arduino.h>
#include <SPI.h>
#include <cstdint>
#include <esp_log.h>

DEFINE_LOG_TAG("DISP");

static ChayaEpdPanel display(GxEPD2_154c_GDEM0154F51H(/*CS=*/ pins::kSpiCs, /*DC=*/ pins::kDisplayDc,
                                                        /*RST=*/ pins::kDisplayRst,
                                                        /*BUSY=*/ pins::kDisplayBusy));

static bool     s_panelInited    = false;
static bool     s_hibernating    = false;
static uint32_t s_busyCbLastLogMs = 0;

ChayaEpdPanel& displayPanel() {
    return display;
}

static void displayBusyProgressCb(const void*) {
    const uint32_t now = millis();
    if (now - s_busyCbLastLogMs >= 2000U) {
        s_busyCbLastLogMs = now;
        ESP_LOGI(TAG, "EPD busy… pin=%d pwr_en=%d", digitalRead(pins::kDisplayBusy),
                 digitalRead(pins::kDisplayPwrEn));
    }
    // GxEPD2 skips its internal delay(1) when a busy callback is set; yield here.
    delay(1);
}

static void displaySetPanelPower(bool on) {
    // Waveshare 08_E_paper_test GPIO_Config / ESP-IDF POWEER_EPD_ON: EPD_PWR LOW = on.
    pinMode(pins::kDisplayPwrEn, OUTPUT);
    digitalWrite(pins::kDisplayPwrEn, on ? LOW : HIGH);
}

static void displayAttachSpiPins() {
    // Must run before GxEPD2 init(). init() calls SPI.begin() with ESP32-S3 defaults
    // (MOSI=11, MISO=13) which swap CS and SDI. begin() is a no-op if already started.
    SPI.begin(/*SCK=*/ pins::kSpiSck, /*MISO=*/ pins::kSpiMiso, /*MOSI=*/ pins::kSpiMosi,
              /*SS=*/ pins::kSpiCs);
}

void displayInitGxEpd() {
    static constexpr uint32_t kEpdSerialDiagOff   = 0;
    static constexpr bool     kEpdInitialFull     = true;
    // Waveshare "clever" reset: RST LOW ~2 ms (GxEPD2 + official EPD_1IN54G_Reset).
    static constexpr uint16_t kEpdResetDurationMs = 2;
    static constexpr bool     kEpdPulldownRst     = false;

    displayAttachSpiPins();
    display.init(kEpdSerialDiagOff, kEpdInitialFull, kEpdResetDurationMs, kEpdPulldownRst);
    // GxEPD2 init() calls SPI.begin() with no pins; keep Waveshare SCK/MOSI/CS mapping.
    displayAttachSpiPins();
    pinMode(pins::kDisplayBusy, INPUT);
    display.epd2.setBusyCallback(displayBusyProgressCb, nullptr);
    s_busyCbLastLogMs = 0;
    s_panelInited     = true;
    s_hibernating     = false;
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
    // Waveshare 08_E_paper_test + GxEPD2: keep EPD3V3_EN on; never SPI.end() or
    // rail-cycle between frames. Hibernate wakes with init() + RST only.
    if (!s_hibernating && s_panelInited) {
        return;
    }
    displayInitGxEpd();
}

void displaySuspendSpiLowPower() {
    display.epd2.setBusyCallback(nullptr, nullptr);
    s_hibernating = true;
}
