#include "display.h"
#include "internal.h"

#include "hw/button.h"
#include "hw/pins.h"
#include "web/auth.h"

#include <GxEPD2_3C.h>
#include <Arduino.h>
#include <SPI.h>
#include <atomic>
#include <driver/gpio.h>

static ChayaEpdPanel display(GxEPD2_154_Z90c(/*CS=*/ pins::kSpiCs, /*DC=*/ pins::kDisplayDc,
                                              /*RST=*/ pins::kDisplayRst,
                                              /*BUSY=*/ pins::kDisplayBusy));

static std::atomic<bool> g_heartRedrawPending{false};
static std::atomic<bool>     g_deferredAuthCodePending{false};
static std::atomic<uint32_t> g_deferredAuthCodeValue{0};
static std::atomic<bool>     g_deferredAuthPromptPending{false};
static std::atomic<bool>     g_deferredSplashPending{false};
static std::atomic<bool>     g_deferredHeartScreenPending{false};
static bool                  g_displaySpiSuspendedLowPower = false;

ChayaEpdPanel& displayPanel() {
    return display;
}

void displayResumeSpiForDraw() {
    if (g_displaySpiSuspendedLowPower) {
        gpio_hold_dis(static_cast<gpio_num_t>(pins::kSpiCs));
        g_displaySpiSuspendedLowPower = false;
    }
    SPI.begin(/*SCK=*/ pins::kSpiSck, /*MISO=*/ pins::kSpiMiso, /*MOSI=*/ pins::kSpiMosi,
              /*SS=*/ pins::kSpiCs);
}

void displaySuspendSpiLowPower() {
    SPI.end();
    pinMode(pins::kSpiSck, INPUT_PULLDOWN);
    pinMode(pins::kSpiMosi, INPUT_PULLDOWN);
    pinMode(pins::kSpiMiso, INPUT_PULLDOWN);
    pinMode(pins::kSpiCs, OUTPUT);
    digitalWrite(pins::kSpiCs, HIGH);
    gpio_hold_en(static_cast<gpio_num_t>(pins::kSpiCs));
    g_displaySpiSuspendedLowPower = true;
}

void requestHeartRedraw() {
    g_heartRedrawPending.store(true, std::memory_order_release);
}

bool consumeHeartRedraw() {
    return g_heartRedrawPending.exchange(false, std::memory_order_acq_rel);
}

void requestDeferredDrawAuthCode(uint32_t code) {
    g_deferredAuthCodeValue.store(code, std::memory_order_relaxed);
    g_deferredAuthCodePending.store(true, std::memory_order_release);
}

void requestDeferredDrawAuthPrompt() {
    g_deferredAuthPromptPending.store(true, std::memory_order_release);
}

void requestDeferredDrawSplashScreen() {
    g_deferredSplashPending.store(true, std::memory_order_release);
}

void requestDeferredDrawHeartScreen() {
    g_deferredHeartScreenPending.store(true, std::memory_order_release);
}

void displayProcessDeferredDrawsOnMainTask() {
    if (g_deferredAuthCodePending.exchange(false, std::memory_order_acq_rel)) {
        drawAuthCode(g_deferredAuthCodeValue.load(std::memory_order_relaxed));
        buttonSetAuthBlinkActive(true);
        return;
    }
    if (g_deferredAuthPromptPending.exchange(false, std::memory_order_acq_rel)) {
        drawAuthPrompt();
        webAuthResetConfirmDeadline();
        buttonSetAuthBlinkActive(true);
        return;
    }
    if (g_deferredSplashPending.exchange(false, std::memory_order_acq_rel)) {
        drawSplashScreen();
        return;
    }
    if (g_deferredHeartScreenPending.exchange(false, std::memory_order_acq_rel)) {
        drawHeartWithNumber();
    }
}

void displayInit() {
    SPI.begin(/*SCK=*/ pins::kSpiSck, /*MISO=*/ pins::kSpiMiso, /*MOSI=*/ pins::kSpiMosi,
              /*SS=*/ pins::kSpiCs);
    /*
     * Do not register loopTask with esp_task_wdt: full-window 3C e-paper refresh can block >5s inside
     * nextPage(), which would trigger a task WDT abort. Long draws are expected on this device.
     */
    /* Baud 0: avoid GxEPD2 UART spam ("Update_Full") on Serial; use ESP_LOG only in debug builds. */
    display.init(0, true, 2, false);
}
