#include "button.h"

#include "button_config.h"
#include "button_internal.h"

#include "async/task_config.h"
#include "config/app_config.h"
#include "display/display.h"
#include "heart/counter.h"
#include "hw/battery.h"
#include "mqtt/config.h"
#include "ota/ota.h"
#include "web/admin_globals.h"
#include "wifi/wlan.h"

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
ButtonState               btn{};
PwrButtonState            pwr{};

static void waitForPwrRelease() {
    unsigned long releasedSinceMs = 0;
    for (;;) {
        const unsigned long nowMs = millis();
        if (digitalRead(pins::kPwrButton) != LOW) {
            if (releasedSinceMs == 0) {
                releasedSinceMs = nowMs;
            } else if (nowMs - releasedSinceMs >= kDebounceStableMs) {
                return;
            }
        } else {
            releasedSinceMs = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void processPowerOff() {
    if (otaBlocksDestructiveAction()) {
        ESP_LOGW(TAG, "PWR soft-off ignored: OTA in progress");
        return;
    }

    g_systemShutdownInProgress.store(true, std::memory_order_release);
    flushHeartCounterIfDirty();
    flushHeartSentCounterIfDirty();

    // A four-color full refresh can take tens of seconds. This task intentionally stops all
    // button processing while shutdown is committed, so unsubscribe it from the task watchdog.
    chayaTaskWatchdogUnsubscribe(TAG);
    if (!displayDrawPowerOffAndWait(kPowerOffDisplayTimeoutMs)) {
        ESP_LOGW(TAG, "Power-off screen timed out; continuing shutdown");
    }

    // Deep-sleep wake is level-triggered. Sleeping while PWR is still LOW would wake immediately.
    ESP_LOGI(TAG, "PWR released after soft-off — waiting for stable release then deep sleep");
    waitForPwrRelease();
    ESP_LOGI(TAG, "PWR released after soft-off — entering deep sleep (mv=%d pct=%d)",
             batteryMilliVolts(), batteryPercent());
    batteryPowerOffAndSleep();
}

void pwrPollAndProcess() {
    const int raw             = digitalRead(pins::kPwrButton);
    const unsigned long nowMs = millis();
    if (raw != pwr.lastRawReading) {
        pwr.lastRawReading       = raw;
        pwr.lastDebounceChangeMs = nowMs;
    }
    if (nowMs - pwr.lastDebounceChangeMs >= kDebounceStableMs) {
        pwr.debouncedLevel = pwr.lastRawReading;
    }
    const bool pressed = (pwr.debouncedLevel == LOW);
    if (!pwr.seenRelease) {
        if (!pressed) {
            pwr.seenRelease = true;
        }
        return;
    }
    if (pressed) {
        if (!pwr.heldDown) {
            pwr.heldDown         = true;
            pwr.softOffTriggered = false;
            pwr.pressStartMs     = nowMs;
        } else if (!pwr.softOffTriggered && (nowMs - pwr.pressStartMs >= kSoftOffHoldMs)) {
            pwr.softOffTriggered = true;
            ESP_LOGI(TAG, "PWR long press: soft-off");
            processPowerOff();
        }
    } else {
        pwr.heldDown         = false;
        pwr.softOffTriggered = false;
    }
}

void buttonPollAndProcess() {
    const int raw             = digitalRead(kButtonGpio);
    const unsigned long nowMs = millis();
    if (raw != btn.lastRawReading) {
        btn.lastRawReading       = raw;
        btn.lastDebounceChangeMs = nowMs;
    }
    if (nowMs - btn.lastDebounceChangeMs >= kDebounceStableMs) {
        btn.debouncedLevel = btn.lastRawReading;
    }
    // BOOT / Key1 is active-low (pressed = LOW).
    const bool pressed = (btn.debouncedLevel == LOW);

    if (pressed) {
        if (!btn.heldDown) {
            btn.heldDown     = true;
            btn.pressStartMs = nowMs;
        }
    } else {
        if (btn.heldDown) {
            const unsigned long held = nowMs - btn.pressStartMs;
            if (held >= kShortPressMinMs) {
                if (!ledTxBusy() && !configIsApMode()) {
                    if (mqttCfgIsBrokerConfigured()) {
                        startMqttSendLedSequence();
                    } else {
                        ESP_LOGD(TAG, "BTN publish skipped: ap=0 broker=0 ledBusy=0");
                    }
                } else {
                    ESP_LOGD(TAG, "BTN publish skipped: ap=%d broker=%d ledBusy=%d",
                             configIsApMode() ? 1 : 0, mqttCfgIsBrokerConfigured() ? 1 : 0,
                             ledTxBusy() ? 1 : 0);
                }
            }
            btn.heldDown = false;
        }
    }
}

static void IRAM_ATTR buttonIsrHandler(void*) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    const TaskHandle_t h = s_buttonTaskHandle.load(std::memory_order_acquire);
    if (h != nullptr) {
        vTaskNotifyGiveFromISR(h, &xHigherPriorityTaskWoken);
    }
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

static void buttonTaskFn(void*) {
    chayaTaskWatchdogSubscribe(TAG);
    static uint32_t s_stackLogCounter = 0;
    for (;;) {
        const unsigned long waitMs =
            ledActivityActive() ? kButtonTaskPollActiveMs : kButtonTaskPollIdleMs;
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(waitMs));
        buttonPollAndProcess();
        pwrPollAndProcess();
        advanceLedSequence();
        chayaTaskWatchdogReset();
        logTaskStackHighWaterPeriodic(TAG, s_stackLogCounter, 600);
    }
}

void buttonInit() {
    pinMode(kButtonGpio, INPUT_PULLUP);
    pinMode(pins::kPwrButton, INPUT_PULLUP);
    pinMode(kButtonLedPin, OUTPUT);
    ledOutput(LOW);  // active-low LED off
    btn.lastRawReading       = digitalRead(kButtonGpio);
    btn.debouncedLevel       = btn.lastRawReading;
    btn.lastDebounceChangeMs = millis();
    pwr.lastRawReading       = digitalRead(pins::kPwrButton);
    pwr.debouncedLevel       = pwr.lastRawReading;
    pwr.lastDebounceChangeMs = millis();
    pwr.seenRelease          = (pwr.debouncedLevel != LOW);
}

void buttonStartTask() {
    TaskHandle_t th = nullptr;
    const BaseType_t ok =
        xTaskCreatePinnedToCore(buttonTaskFn, "button", kButtonTaskStackBytes, nullptr, 8, &th, 1);
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
    if (!configGetLedEnabled()) {
        ledOutput(LOW);
        return;
    }
    for (int i = 0; i < kButtonStartupBlinkCount; i++) {
        ledOutput(HIGH);
        vTaskDelay(pdMS_TO_TICKS(kButtonStartupBlinkMs));
        ledOutput(LOW);
        vTaskDelay(pdMS_TO_TICKS(kButtonStartupBlinkMs));
    }
}

void buttonEnableLedGpioHoldForLightSleep() {
    ledHoldWhenIdle();
}
