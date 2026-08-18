#include "display.h"
#include "internal.h"

#include "async/event_types.h"
#include "async/task_handles.h"
#include "heart/counter.h"
#include "hw/pins.h"

#include "async/task_config.h"
#include "diag/stack_monitor.h"
#include "util/log_tag.h"

#include <Arduino.h>
#include <SPI.h>
#include <atomic>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <driver/gpio.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

DEFINE_LOG_TAG("DISP");

// Only this task uses SPI/EPD; everyone else posts DisplayMsg.

static ChayaEpdPanel display(GxEPD2_154c_GDEM0154F51H(/*CS=*/ pins::kSpiCs, /*DC=*/ pins::kDisplayDc,
                                                        /*RST=*/ pins::kDisplayRst,
                                                        /*BUSY=*/ pins::kDisplayBusy));

static bool g_displaySpiSuspendedLowPower = false;
static std::atomic<bool> s_heartDrawQueued{false};
static std::atomic<int> s_lastDrawnRx{INT32_MIN};
static std::atomic<int> s_lastDrawnTx{INT32_MIN};
static std::atomic<unsigned long> s_lastHeartRedrawEnqueueMs{0};
static constexpr unsigned long    kHeartRedrawMinIntervalMs = 30000UL;

static bool displayPostHeartRedraw(TickType_t waitTicks, bool bypassMinInterval = false) {
    const int rx = heartCounter.load(std::memory_order_relaxed);
    const int tx = heartSentCounter.load(std::memory_order_relaxed);
    if (rx == s_lastDrawnRx.load(std::memory_order_relaxed)
        && tx == s_lastDrawnTx.load(std::memory_order_relaxed)) {
        return true;
    }
    const unsigned long nowMs = millis();
    const unsigned long lastMs = s_lastHeartRedrawEnqueueMs.load(std::memory_order_relaxed);
    if (!bypassMinInterval && lastMs != 0UL && (nowMs - lastMs) < kHeartRedrawMinIntervalMs) {
        return false;
    }
    if (s_heartDrawQueued.exchange(true, std::memory_order_acq_rel)) {
        return true;
    }
    DisplayMsg msg{DisplayMsg::Cmd::DrawHeart, 0};
    if (xQueueSend(g_displayCmdQueue, &msg, waitTicks) != pdTRUE) {
        s_heartDrawQueued.store(false, std::memory_order_release);
        ESP_LOGW(TAG, "display queue full (cmd=%d)", static_cast<int>(DisplayMsg::Cmd::DrawHeart));
        return false;
    }
    s_lastHeartRedrawEnqueueMs.store(nowMs, std::memory_order_relaxed);
    return true;
}

ChayaEpdPanel& displayPanel() {
    return display;
}

void displayHwInitPins() {
    // EPD3V3_EN is active-low: drive LOW before any SPI/EPD traffic.
    pinMode(pins::kDisplayPwrEn, OUTPUT);
    digitalWrite(pins::kDisplayPwrEn, LOW);
    pinMode(pins::kDisplayBusy, INPUT);
    pinMode(pins::kDisplayRst, OUTPUT);
    pinMode(pins::kDisplayDc, OUTPUT);
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
    digitalWrite(pins::kDisplayPwrEn, LOW);
    SPI.begin(/*SCK=*/ pins::kSpiSck, /*MISO=*/ pins::kSpiMiso, /*MOSI=*/ pins::kSpiMosi,
              /*SS=*/ pins::kSpiCs);
}

void displaySuspendSpiLowPower() {
    SPI.end();
    pinMode(pins::kSpiSck, INPUT_PULLDOWN);
    pinMode(pins::kSpiMosi, INPUT_PULLDOWN);
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
        case DisplayMsg::Cmd::DrawHeart: {
            drawHeartWithNumber();
            const int drawnRx = heartCounter.load(std::memory_order_relaxed);
            const int drawnTx = heartSentCounter.load(std::memory_order_relaxed);
            s_lastDrawnRx.store(drawnRx, std::memory_order_relaxed);
            s_lastDrawnTx.store(drawnTx, std::memory_order_relaxed);
            s_heartDrawQueued.store(false, std::memory_order_release);
            if (heartCounter.load(std::memory_order_relaxed) != drawnRx
                || heartSentCounter.load(std::memory_order_relaxed) != drawnTx) {
                (void)displayPostHeartRedraw(0, true);
            }
            break;
        }
        case DisplayMsg::Cmd::DrawSplash:
            drawSplashScreen();
            break;
        }
        logTaskStackHighWaterPeriodic("DISP", s_stackLogCounter, 600);
    }
}

static bool displayPostMsg(DisplayMsg::Cmd cmd, uint32_t payload, TickType_t waitTicks) {
    DisplayMsg msg{cmd, payload};
    if (xQueueSend(g_displayCmdQueue, &msg, waitTicks) != pdTRUE) {
        ESP_LOGW(TAG, "display queue full (cmd=%d)", static_cast<int>(cmd));
        return false;
    }
    return true;
}

void requestHeartRedraw() {
    (void)displayPostHeartRedraw(pdMS_TO_TICKS(100));
}

void requestHeartRedrawNonBlocking() {
    (void)displayPostHeartRedraw(0);
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
     * Do not register loopTask with esp_task_wdt: full-window 4C e-paper refresh can block ~20s
     * inside nextPage(), which would trigger a task WDT abort. Long draws are expected on this device.
     */
    static constexpr uint32_t  kEpdSerialDiagOff    = 0;
    static constexpr bool      kEpdInitialFull      = false;
    static constexpr uint16_t  kEpdResetDurationMs = 2;
    static constexpr bool      kEpdPulldownRst       = false;
    display.init(kEpdSerialDiagOff, kEpdInitialFull, kEpdResetDurationMs, kEpdPulldownRst);
}

void displayStartTask() {
    const BaseType_t ok =
        xTaskCreatePinnedToCore(displayTaskFn, "display", kDisplayTaskStackBytes, nullptr, 3, nullptr, 1);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "display task create failed");
        abort();
    }
}
