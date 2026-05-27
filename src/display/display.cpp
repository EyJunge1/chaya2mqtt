#include "display.h"
#include "internal.h"

#include "async/event_types.h"
#include "async/task_handles.h"
#include "hw/button.h"
#include "hw/pins.h"
// Nach Auth-UI: Session-Fenster + LED koordinieren (siehe web/auth.cpp — bewusste Kopplung).
#include "web/auth.h"

#include "diag/stack_monitor.h"
#include "log_tag.h"

#include <Arduino.h>
#include <SPI.h>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <driver/gpio.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

DEFINE_LOG_TAG("DISP");

// Only this task uses SPI/EPD; everyone else posts DisplayMsg.

static ChayaEpdPanel display(/*CS=*/ pins::kSpiCs, /*DC=*/ pins::kDisplayDc,
                             /*RST=*/ pins::kDisplayRst, /*BUSY=*/ pins::kDisplayBusy);

static bool g_displaySpiSuspendedLowPower = false;
static std::atomic<bool> s_heartDrawQueued{false};

static bool displayPostHeartRedraw(TickType_t waitTicks) {
    if (s_heartDrawQueued.exchange(true, std::memory_order_acq_rel)) {
        return true;
    }
    DisplayMsg msg{DisplayMsg::Cmd::DrawHeart, 0};
    if (xQueueSend(g_displayCmdQueue, &msg, waitTicks) != pdTRUE) {
        s_heartDrawQueued.store(false, std::memory_order_release);
        ESP_LOGW(TAG, "display queue full (cmd=%d)", static_cast<int>(DisplayMsg::Cmd::DrawHeart));
        return false;
    }
    return true;
}

ChayaEpdPanel& displayPanel() {
    return display;
}

void displayHwInitPins() {
    pinMode(pins::kDisplayBusy, INPUT);
    pinMode(pins::kDisplayRst, OUTPUT);
    pinMode(pins::kDisplayDc, OUTPUT);
    pinMode(pins::kSpiMiso, INPUT);
    pinMode(pins::kSpiSck, OUTPUT);
    pinMode(pins::kSpiMosi, OUTPUT);
    pinMode(pins::kSpiCs, OUTPUT);
    digitalWrite(pins::kSpiCs, HIGH);
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

static void displayTaskFn(void*) {
    /* No esp_task_wdt on this task: E-Ink full refresh can block >> default TWDT interval (see displayInit). */
    static uint32_t s_stackLogCounter = 0;
    for (;;) {
        DisplayMsg msg;
        if (xQueueReceive(g_displayCmdQueue, &msg, portMAX_DELAY) != pdTRUE) {
            logTaskStackHighWaterPeriodic("DISP", s_stackLogCounter, 600);
            continue;
        }
        switch (msg.cmd) {
        case DisplayMsg::Cmd::DrawHeart:
            drawHeartWithNumber();
            s_heartDrawQueued.store(false, std::memory_order_release);
            break;
        case DisplayMsg::Cmd::DrawSplash:
            drawSplashScreen();
            break;
        case DisplayMsg::Cmd::DrawAuthCode:
            drawAuthCode(msg.payload);
            buttonSetAuthBlinkActive(true);
            break;
        case DisplayMsg::Cmd::DrawAuthPrompt:
            drawAuthPrompt();
            webAuthResetConfirmDeadline();
            buttonSetAuthBlinkActive(true);
            break;
        }
        logTaskStackHighWaterPeriodic("DISP", s_stackLogCounter, 600);
    }
}

static void displayPostMsg(DisplayMsg::Cmd cmd, uint32_t payload, TickType_t waitTicks) {
    DisplayMsg msg{cmd, payload};
    if (xQueueSend(g_displayCmdQueue, &msg, waitTicks) != pdTRUE) {
        ESP_LOGW(TAG, "display queue full (cmd=%d)", static_cast<int>(cmd));
    }
}

void requestHeartRedraw() {
    (void)displayPostHeartRedraw(pdMS_TO_TICKS(100));
}

void requestHeartRedrawNonBlocking() {
    (void)displayPostHeartRedraw(0);
}

void requestDeferredDrawAuthCode(uint32_t code) {
    displayPostMsg(DisplayMsg::Cmd::DrawAuthCode, code, pdMS_TO_TICKS(2000));
}

void requestDeferredDrawAuthPrompt() {
    displayPostMsg(DisplayMsg::Cmd::DrawAuthPrompt, 0, pdMS_TO_TICKS(2000));
}

void requestDeferredDrawSplashScreen() {
    displayPostMsg(DisplayMsg::Cmd::DrawSplash, 0, pdMS_TO_TICKS(100));
}

void requestDeferredDrawHeartScreen() {
    displayPostMsg(DisplayMsg::Cmd::DrawHeart, 0, pdMS_TO_TICKS(100));
}

void displayInit() {
    displayHwInitPins();
    SPI.begin(/*SCK=*/ pins::kSpiSck, /*MISO=*/ pins::kSpiMiso, /*MOSI=*/ pins::kSpiMosi,
              /*SS=*/ pins::kSpiCs);
    /*
     * GxEPD2-style: serial_diag_bitrate, initial_full_refresh, reset_duration_ms, pulldown_rst_mode.
     * Do not register loopTask with esp_task_wdt: full-window 3C e-paper refresh can block >5s inside
     * nextPage(), which would trigger a task WDT abort. Long draws are expected on this device.
     */
    static constexpr uint32_t  kEpdSerialDiagOff    = 0;
    static constexpr bool      kEpdInitialFull      = true;
    static constexpr uint16_t  kEpdResetDurationMs = 2;
    static constexpr bool      kEpdPulldownRst       = false;
    display.init(kEpdSerialDiagOff, kEpdInitialFull, kEpdResetDurationMs, kEpdPulldownRst);
}

void displayStartTask() {
    const BaseType_t ok =
        xTaskCreatePinnedToCore(displayTaskFn, "display", 4096, nullptr, 3, nullptr, 1);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "display task create failed");
        abort();
    }
}
