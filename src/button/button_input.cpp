#include "button.h"

#include "button_actions.h"
#include "button_config.h"
#include "button_internal.h"
#include "button_soft_off_pure.h"

#include "async/system_lifecycle.h"
#include "async/task_config.h"
#include "battery/battery.h"
#include "display/display.h"
#include "heart/counter.h"
#include "hw/pins.h"
#include "led/led.h"
#include "led/led_internal.h"

#include "diag/stack_monitor.h"
#include "diag/task_watchdog.h"

#include <atomic>
#include <cstdlib>

#include <Arduino.h>
#include <driver/gpio.h>
#include <esp_err.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "util/log_tag.h"

DEFINE_LOG_TAG("BTN");

std::atomic<TaskHandle_t> s_buttonTaskHandle{nullptr};
ButtonState btn{};
PwrButtonState pwr{};

/** Blocking SoftOff LED ack while still held (≥2 s). Queued patterns would stall during shutdown. */
static void blinkSoftOffArmedLed() { ledPlayPresetBlocking(LedPreset::SoftOff); }

/** Wait until PWR is stably HIGH. Timeout cuts the latch and keeps waiting (KEEP_AWAKE). */
static void waitForPwrRelease() {
    SoftOffReleaseSettle settle{};
    const unsigned long startedMs = millis();
    bool cutLatchOnTimeout = false;
    for (;;) {
        const unsigned long nowMs = millis();
        if (!cutLatchOnTimeout && nowMs - startedMs >= kSoftOffReleaseTimeoutMs) {
            ESP_LOGW(TAG, "PWR soft-off: release timeout (%lu ms) — cutting latch, still waiting",
                     kSoftOffReleaseTimeoutMs);
            batteryCutLatch();
            cutLatchOnTimeout = true;
        }
        if (softOffReleaseSettled(settle, digitalRead(pins::kPwrButton), nowMs, kSoftOffReleaseSettleMs)) {
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/**
 * Soft-off after long-press release. Returns false if blocked (e.g. OTA).
 * On success does not return (deep sleep / power cut).
 */
static bool processPowerOff() {
    const ButtonActionHooks& hooks = buttonActionHooks();
    if (hooks.softOffAllowed != nullptr && !hooks.softOffAllowed()) {
        static unsigned long s_lastBlockWarnMs = 0;
        const unsigned long nowMs = millis();
        if (s_lastBlockWarnMs == 0 || nowMs - s_lastBlockWarnMs >= 5000UL) {
            ESP_LOGW(TAG, "PWR soft-off ignored: policy blocked");
            s_lastBlockWarnMs = nowMs;
        }
        return false;
    }

    ESP_LOGI(TAG, "PWR long press released: soft-off");

    g_systemShutdownInProgress.store(true, std::memory_order_release);
    flushAllHeartCountersIfDirty();

    // Already released (edge-on-release). EPD may take tens of seconds.
    chayaTaskWatchdogUnsubscribe(TAG);
    if (!displayRequest(DisplayMsg::Cmd::DrawPowerOff, DisplayRequestMode::PowerOffWait, kPowerOffDisplayTimeoutMs)) {
        ESP_LOGW(TAG, "Power-off screen timed out; continuing shutdown");
    }

    // Settle before EXT1 in case of bounce / re-press during the EPD refresh.
    ESP_LOGI(TAG, "PWR soft-off — stable release settle (%lu ms) then deep sleep", kSoftOffReleaseSettleMs);
    waitForPwrRelease();
    ESP_LOGI(TAG, "PWR soft-off — entering deep sleep (mv=%d pct=%d)", batteryMilliVolts(), batteryPercent());
    if (hooks.performSoftOff != nullptr) {
        hooks.performSoftOff();
    } else {
        batteryPowerOffAndSleep();
    }
    return true;
}

void pwrPollAndProcess() {
    const int raw = digitalRead(pins::kPwrButton);
    const unsigned long nowMs = millis();
    debounceUpdate(pwr.debounce, raw, nowMs, kDebounceStableMs);
    const bool pressed = (pwr.debounce.debouncedLevel == LOW);
    if (!pwr.seenRelease) {
        if (!pressed) {
            pwr.seenRelease = true;
        }
        return;
    }
    if (pressed) {
        if (!pwr.heldDown) {
            pwr.heldDown = true;
            pwr.softOffArmed = false;
            pwr.pressStartMs = nowMs;
        } else if (!pwr.softOffArmed && (nowMs - pwr.pressStartMs >= kSoftOffHoldMs)) {
            // Threshold reached — holding longer is fine; shutdown starts on release.
            pwr.softOffArmed = true;
            ESP_LOGI(TAG, "PWR soft-off armed (release to shut down)");
            blinkSoftOffArmedLed();
        }
    } else if (pwr.heldDown) {
        const bool armed = pwr.softOffArmed;
        pwr.heldDown = false;
        pwr.softOffArmed = false;
        if (armed) {
            (void)processPowerOff();
        }
    }
}

void buttonPollAndProcess() {
    const int raw = digitalRead(kButtonGpio);
    const unsigned long nowMs = millis();
    debounceUpdate(btn.debounce, raw, nowMs, kDebounceStableMs);
    // BOOT / Key1 is active-low (pressed = LOW).
    const bool pressed = (btn.debounce.debouncedLevel == LOW);

    if (pressed) {
        if (!btn.heldDown) {
            btn.heldDown = true;
            btn.pressStartMs = nowMs;
        }
    } else {
        if (btn.heldDown) {
            const unsigned long held = nowMs - btn.pressStartMs;
            if (held >= kShortPressMinMs) {
                const ButtonActionHooks& hooks = buttonActionHooks();
                if (hooks.requestSend != nullptr) {
                    hooks.requestSend();
                }
            }
            btn.heldDown = false;
        }
    }
}

static void IRAM_ATTR buttonIsrHandler(void *) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    const TaskHandle_t h = s_buttonTaskHandle.load(std::memory_order_acquire);
    if (h != nullptr) {
        vTaskNotifyGiveFromISR(h, &xHigherPriorityTaskWoken);
    }
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

static void buttonTaskFn(void *) {
    chayaTaskWatchdogSubscribe(TAG);
    static uint32_t s_stackLogCounter = 0;
    for (;;) {
        const unsigned long waitMs = ledIsActivityActive() ? kButtonTaskPollActiveMs : kButtonTaskPollIdleMs;
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(waitMs));
        buttonPollAndProcess();
        pwrPollAndProcess();
        advanceLedSequence();
        chayaTaskWatchdogReset();
        logTaskStackHighWaterPeriodic(TAG, s_stackLogCounter, 600);
    }
}

void buttonNotifyTask() {
    const TaskHandle_t h = s_buttonTaskHandle.load(std::memory_order_acquire);
    if (h != nullptr) {
        xTaskNotifyGive(h);
    }
}

void buttonInit() {
    pinMode(kButtonGpio, INPUT_PULLUP);
    pinMode(pins::kPwrButton, INPUT_PULLUP);
    ledInit();
    const unsigned long nowMs = millis();
    btn.debounce.lastRawReading = digitalRead(kButtonGpio);
    btn.debounce.debouncedLevel = btn.debounce.lastRawReading;
    btn.debounce.lastDebounceChangeMs = nowMs;
    pwr.debounce.lastRawReading = digitalRead(pins::kPwrButton);
    pwr.debounce.debouncedLevel = pwr.debounce.lastRawReading;
    pwr.debounce.lastDebounceChangeMs = nowMs;
    pwr.seenRelease = (pwr.debounce.debouncedLevel != LOW);
}

void buttonStartTask() {
    TaskHandle_t th = nullptr;
    const BaseType_t ok = xTaskCreatePinnedToCore(buttonTaskFn, "button", kButtonTaskStackBytes, nullptr, 8, &th, 1);
    if (ok != pdPASS || th == nullptr) {
        ESP_LOGE(TAG, "button task create failed");
        abort();
    }
    s_buttonTaskHandle.store(th, std::memory_order_release);

    const esp_err_t isr = gpio_install_isr_service(0);
    if (isr != ESP_OK && isr != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "gpio_install_isr_service: %s", esp_err_to_name(isr));
        abort();
    }
    gpio_set_intr_type(static_cast<gpio_num_t>(kButtonGpio), GPIO_INTR_ANYEDGE);
    gpio_isr_handler_add(static_cast<gpio_num_t>(kButtonGpio), buttonIsrHandler, nullptr);
}

void buttonStartupBlink() {
    // Blocking before the button task starts (avoids racing ledOutput with the task).
    ledPlayPresetBlocking(LedPreset::Boot);
}
