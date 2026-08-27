#include "display.h"
#include "internal.h"

#include "async/event_types.h"
#include "async/task_handles.h"
#include "config/app_config.h"
#include "display_refresh_pure.h"
#include "heart/counter.h"
#include "hw/button.h"
#include "hw/pins.h"

#include "async/task_config.h"
#include "diag/stack_monitor.h"
#include "util/log_tag.h"
#include "wifi/wlan.h"

#include <Arduino.h>
#include <SPI.h>
#include <atomic>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

DEFINE_LOG_TAG("DISP");

// Only this task uses SPI/EPD; everyone else posts DisplayMsg.

static ChayaEpdPanel display(GxEPD2_154c_GDEM0154F51H(/*CS=*/ pins::kSpiCs, /*DC=*/ pins::kDisplayDc,
                                                        /*RST=*/ pins::kDisplayRst,
                                                        /*BUSY=*/ pins::kDisplayBusy));

static bool s_panelInited  = false;
static bool s_hibernating  = false;
static std::atomic<bool> s_heartDrawQueued{false};
static std::atomic<bool> s_heartDrawPending{false};
static std::atomic<int> s_lastDrawnRx{INT32_MIN};
static std::atomic<int> s_lastDrawnTx{INT32_MIN};
static std::atomic<unsigned long> s_lastHeartRedrawEnqueueMs{0};
static std::atomic<bool>          s_powerOffPending{false};
static SemaphoreHandle_t          s_drawIdleSem             = nullptr;
static SemaphoreHandle_t          s_powerOffDoneSem         = nullptr;
static SemaphoreHandle_t          s_displayPostMutex        = nullptr;
static uint32_t                   s_busyCbLastLogMs         = 0;
static constexpr uint32_t         kDrawOnlyIfViewChanged    = 1U;

static bool displayPostMsg(DisplayMsg::Cmd cmd, uint32_t payload, TickType_t waitTicks);

static bool displayPostHeartRedraw(TickType_t waitTicks) {
    if (s_powerOffPending.load(std::memory_order_acquire)) {
        return false;
    }
    const int rx = heartCounter.load(std::memory_order_relaxed);
    const int tx = heartSentCounter.load(std::memory_order_relaxed);
    const unsigned long nowMs = millis();
    const unsigned long lastMs = s_lastHeartRedrawEnqueueMs.load(std::memory_order_relaxed);
    const DisplayHeartRedrawDecision decision = displayHeartRedrawDecide(
        rx, tx, s_lastDrawnRx.load(std::memory_order_relaxed),
        s_lastDrawnTx.load(std::memory_order_relaxed), nowMs, lastMs,
        kHeartRedrawMinIntervalMs);
    if (decision == DisplayHeartRedrawDecision::SkipUnchanged) {
        return true;
    }
    if (decision == DisplayHeartRedrawDecision::DeferPending) {
        s_heartDrawPending.store(true, std::memory_order_release);
        return false;
    }
    if (s_heartDrawQueued.exchange(true, std::memory_order_acq_rel)) {
        // A draw is already queued/running; remember that counters may still move.
        s_heartDrawPending.store(true, std::memory_order_release);
        return true;
    }
    if (!displayPostMsg(DisplayMsg::Cmd::DrawHeart, 0, waitTicks)) {
        s_heartDrawQueued.store(false, std::memory_order_release);
        s_heartDrawPending.store(true, std::memory_order_release);
        return false;
    }
    s_lastHeartRedrawEnqueueMs.store(nowMs, std::memory_order_relaxed);
    // Queued draw captures the latest counters at paint time; later changes re-set pending.
    s_heartDrawPending.store(false, std::memory_order_release);
    return true;
}

static TickType_t displayHeartPendingWaitTicks() {
    constexpr unsigned long kIdlePollMs = 1000UL;
    const unsigned long waitMs = displayHeartRedrawWaitMs(
        millis(), s_lastHeartRedrawEnqueueMs.load(std::memory_order_relaxed),
        kHeartRedrawMinIntervalMs, s_heartDrawPending.load(std::memory_order_acquire));
    if (waitMs == ULONG_MAX) {
        // Finite idle poll so a deferred pending flag set by another task is noticed.
        return pdMS_TO_TICKS(kIdlePollMs);
    }
    if (waitMs == 0UL) {
        return 0;
    }
    return pdMS_TO_TICKS(waitMs);
}

ChayaEpdPanel& displayPanel() {
    return display;
}

static void displayBusyProgressCb(const void*) {
    const uint32_t now = millis();
    if (now - s_busyCbLastLogMs < 2000U) {
        return;
    }
    s_busyCbLastLogMs = now;
    ESP_LOGI(TAG, "EPD busy… pin=%d pwr_en=%d", digitalRead(pins::kDisplayBusy),
             digitalRead(pins::kDisplayPwrEn));
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

static void displayInitGxEpd() {
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

static void displaySignalDrawIdle() {
    if (s_drawIdleSem != nullptr) {
        (void)xSemaphoreGive(s_drawIdleSem);
    }
}

static void displayTaskFn(void*) {
    /* No esp_task_wdt on this task: E-Ink full refresh can block >> default TWDT interval (see displayInit). */
    static uint32_t s_stackLogCounter = 0;
    for (;;) {
        DisplayMsg msg;
        const TickType_t waitTicks = displayHeartPendingWaitTicks();
        if (xQueueReceive(g_displayCmdQueue, &msg, waitTicks) != pdTRUE) {
            // Trailing-edge timeout: flush a deferred heart redraw with the latest counters.
            if (s_heartDrawPending.load(std::memory_order_acquire)
                && !s_powerOffPending.load(std::memory_order_acquire)) {
                (void)displayPostHeartRedraw(0);
            }
            logTaskStackHighWaterPeriodic("DISP", s_stackLogCounter, 600);
            continue;
        }
        switch (msg.cmd) {
        case DisplayMsg::Cmd::DrawHeart: {
            if (!displayRefreshRequired(configGetDisplayView(), DisplayView::Heart,
                                        msg.payload == kDrawOnlyIfViewChanged)) {
                ESP_LOGI(TAG, "heart view unchanged; refresh skipped");
                s_heartDrawQueued.store(false, std::memory_order_release);
                if (s_heartDrawPending.exchange(false, std::memory_order_acq_rel)) {
                    (void)displayPostHeartRedraw(0);
                }
                break;
            }
            wlanBeginLowInterferenceForEpd();
            ledRefreshPulseBegin();
            const HeartCounterDrawSnapshot drawn = drawHeartWithNumber();
            ledRefreshPulseEnd();
            wlanEndLowInterferenceForEpd();
            (void)configSetDisplayView(DisplayView::Heart);
            s_lastDrawnRx.store(drawn.heartCounterRaw, std::memory_order_relaxed);
            s_lastDrawnTx.store(drawn.heartSentCounterRaw, std::memory_order_relaxed);
            s_heartDrawQueued.store(false, std::memory_order_release);
            const bool hadPending = s_heartDrawPending.exchange(false, std::memory_order_acq_rel);
            if (displayHeartNeedsFollowUpRedraw(
                    drawn.heartCounterRaw, drawn.heartSentCounterRaw,
                    heartCounter.load(std::memory_order_relaxed),
                    heartSentCounter.load(std::memory_order_relaxed), hadPending)) {
                (void)displayPostHeartRedraw(0);
            }
            break;
        }
        case DisplayMsg::Cmd::DrawSplash: {
            const DisplayView targetView = displaySplashTargetView();
            if (!displayRefreshRequired(configGetDisplayView(), targetView,
                                        msg.payload == kDrawOnlyIfViewChanged)) {
                ESP_LOGI(TAG, "splash view unchanged; refresh skipped");
                if (s_heartDrawPending.exchange(false, std::memory_order_acq_rel)) {
                    (void)displayPostHeartRedraw(0);
                }
                break;
            }
            wlanBeginLowInterferenceForEpd();
            ledRefreshPulseBegin();
            const DisplayView drawnView = drawSplashScreen();
            ledRefreshPulseEnd();
            wlanEndLowInterferenceForEpd();
            (void)configSetDisplayView(drawnView);
            if (s_heartDrawPending.exchange(false, std::memory_order_acq_rel)) {
                (void)displayPostHeartRedraw(0);
            }
            break;
        }
        case DisplayMsg::Cmd::DrawPowerOff:
            if (!displayViewNeedsRefresh(configGetDisplayView(), DisplayView::ProductTitle)) {
                ESP_LOGI(TAG, "power-off view unchanged; refresh skipped");
                s_heartDrawPending.store(false, std::memory_order_release);
                if (s_powerOffDoneSem != nullptr) {
                    (void)xSemaphoreGive(s_powerOffDoneSem);
                }
                break;
            }
            wlanBeginLowInterferenceForEpd();
            ledRefreshPulseBegin();
            drawPowerOffScreen();
            ledRefreshPulseEnd();
            wlanEndLowInterferenceForEpd();
            (void)configSetDisplayView(DisplayView::ProductTitle);
            s_heartDrawPending.store(false, std::memory_order_release);
            if (s_powerOffDoneSem != nullptr) {
                (void)xSemaphoreGive(s_powerOffDoneSem);
            }
            break;
        }
        displaySignalDrawIdle();
        logTaskStackHighWaterPeriodic("DISP", s_stackLogCounter, 600);
    }
}

static bool displayPostMsg(DisplayMsg::Cmd cmd, uint32_t payload, TickType_t waitTicks) {
    if (s_displayPostMutex == nullptr
        || xSemaphoreTake(s_displayPostMutex, waitTicks) != pdTRUE) {
        ESP_LOGW(TAG, "display post mutex unavailable (cmd=%d)", static_cast<int>(cmd));
        return false;
    }
    if (cmd != DisplayMsg::Cmd::DrawPowerOff
        && s_powerOffPending.load(std::memory_order_acquire)) {
        xSemaphoreGive(s_displayPostMutex);
        return false;
    }
    DisplayMsg msg{cmd, payload};
    const bool queued = xQueueSend(g_displayCmdQueue, &msg, waitTicks) == pdTRUE;
    xSemaphoreGive(s_displayPostMutex);
    if (!queued) {
        ESP_LOGW(TAG, "display queue full (cmd=%d)", static_cast<int>(cmd));
        return false;
    }
    return true;
}

void requestHeartRedraw() {
    (void)displayPostHeartRedraw(pdMS_TO_TICKS(100));
}

bool requestHeartRedrawNonBlocking() {
    return displayPostHeartRedraw(0);
}

void requestDeferredDrawSplashScreen() {
    if (s_drawIdleSem != nullptr) {
        while (xSemaphoreTake(s_drawIdleSem, 0) == pdTRUE) {
        }
    }
    ESP_LOGI(TAG, "splash queued");
    displayPostMsg(DisplayMsg::Cmd::DrawSplash, kDrawOnlyIfViewChanged, pdMS_TO_TICKS(100));
}

void requestDeferredDrawHeartScreen() {
    if (s_drawIdleSem != nullptr) {
        while (xSemaphoreTake(s_drawIdleSem, 0) == pdTRUE) {
        }
    }
    displayPostMsg(DisplayMsg::Cmd::DrawHeart, kDrawOnlyIfViewChanged, pdMS_TO_TICKS(100));
}

bool displayWaitDrawIdle(uint32_t timeoutMs) {
    if (s_drawIdleSem == nullptr) {
        return false;
    }
    return xSemaphoreTake(s_drawIdleSem, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

bool displayDrawPowerOffAndWait(uint32_t timeoutMs) {
    if (s_powerOffDoneSem == nullptr || s_displayPostMutex == nullptr) {
        return false;
    }
    while (xSemaphoreTake(s_powerOffDoneSem, 0) == pdTRUE) {
    }
    if (xSemaphoreTake(s_displayPostMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGW(TAG, "power-off display post mutex unavailable");
        return false;
    }
    s_powerOffPending.store(true, std::memory_order_release);
    const DisplayMsg msg{DisplayMsg::Cmd::DrawPowerOff, 0};
    const bool queued =
        xQueueSend(g_displayCmdQueue, &msg, pdMS_TO_TICKS(1000)) == pdTRUE;
    if (!queued) {
        s_powerOffPending.store(false, std::memory_order_release);
    }
    xSemaphoreGive(s_displayPostMutex);
    if (!queued) {
        ESP_LOGW(TAG, "power-off display queue full");
        return false;
    }
    ESP_LOGI(TAG, "power-off screen queued");
    return xSemaphoreTake(s_powerOffDoneSem, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

void displayInit() {
    displayHwInitPins();
    /*
     * GxEPD2-style: serial_diag_bitrate, initial_full_refresh, reset_duration_ms, pulldown_rst_mode.
     * Do not register loopTask with esp_task_wdt: full-window 4C e-paper refresh can block ~20s
     * inside nextPage(), which would trigger a task WDT abort. Long draws are expected on this device.
     */
    displayInitGxEpd();
}

void displayStartTask() {
    if (s_drawIdleSem == nullptr) {
        s_drawIdleSem = xSemaphoreCreateBinary();
        if (s_drawIdleSem == nullptr) {
            ESP_LOGE(TAG, "draw idle semaphore create failed");
            abort();
        }
    }
    if (s_powerOffDoneSem == nullptr) {
        s_powerOffDoneSem = xSemaphoreCreateBinary();
        if (s_powerOffDoneSem == nullptr) {
            ESP_LOGE(TAG, "power-off semaphore create failed");
            abort();
        }
    }
    if (s_displayPostMutex == nullptr) {
        s_displayPostMutex = xSemaphoreCreateMutex();
        if (s_displayPostMutex == nullptr) {
            ESP_LOGE(TAG, "display post mutex create failed");
            abort();
        }
    }
    const BaseType_t ok =
        xTaskCreatePinnedToCore(displayTaskFn, "display", kDisplayTaskStackBytes, nullptr, 3, nullptr, 1);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "display task create failed");
        abort();
    }
}
