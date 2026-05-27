#include "button.h"

#include "button_config.h"
#include "button_internal.h"

#include "async/event_types.h"
#include "async/task_config.h"
#include "async/task_handles.h"
#include "mqtt/config.h"
#include "ota/ota.h"
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

static void triggerFactoryReset() {
    if (otaBlocksDestructiveAction()) {
        ESP_LOGW(TAG, "Factory reset ignored: OTA in progress");
        return;
    }
    for (int i = 0; i < kFactoryResetLedBlinkCycles; i++) {
        ledOutput((i % 2) == 0 ? HIGH : LOW);
        vTaskDelay(pdMS_TO_TICKS(kFactoryResetLedBlinkPeriodMs));
        chayaTaskWatchdogReset();
    }
    ledOutput(LOW);
    if (g_netCmdQueue != nullptr) {
        const NetCmd cmd = NetCmd::FactoryResetRequested;
        if (xQueueSend(g_netCmdQueue, &cmd, pdMS_TO_TICKS(500)) != pdTRUE) {
            ESP_LOGW(TAG, "Factory reset queue full — retry on next hold");
        }
    } else {
        ESP_LOGE(TAG, "Factory reset queue unavailable");
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
    const int reading = btn.debouncedLevel;

    if (reading == HIGH) {
        if (!btn.heldDown) {
            btn.heldDown              = true;
            btn.pressStartMs          = nowMs;
            btn.factoryResetTriggered = false;
        } else if (!btn.factoryResetTriggered && (nowMs - btn.pressStartMs >= kFactoryResetHoldMs)) {
            btn.factoryResetTriggered = true;
            triggerFactoryReset();
        }
    } else {
        if (btn.heldDown) {
            const unsigned long held = nowMs - btn.pressStartMs;
            if (!btn.factoryResetTriggered && held >= kShortPressMinMs && held < kFactoryResetHoldMs) {
                if (buttonIsAuthBlinkActive()) {
                    if (s_authBlinkShortPressHandler != nullptr) {
                        s_authBlinkShortPressHandler();
                    }
                } else if (!ledSendSequenceActive() && !configIsApMode()) {
                    if (mqttCfgIsBrokerConfigured()) {
                        startMqttSendLedSequence();
                    }
                }
            }
            btn.heldDown              = false;
            btn.factoryResetTriggered = false;
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
            ledSendSequenceActive() ? kButtonTaskPollActiveMs : kButtonTaskPollIdleMs;
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(waitMs));
        consumeButtonCommands();
        buttonPollAndProcess();
        advanceLedSequence();
        chayaTaskWatchdogReset();
        logTaskStackHighWaterPeriodic(TAG, s_stackLogCounter, 600);
    }
}

void buttonInit() {
    pinMode(kButtonGpio, INPUT_PULLDOWN);
    pinMode(kButtonLedPin, OUTPUT);
    btn.lastRawReading       = digitalRead(kButtonGpio);
    btn.debouncedLevel       = btn.lastRawReading;
    btn.lastDebounceChangeMs = millis();

    s_buttonCmdQueue = xQueueCreate(4, sizeof(ButtonInternalCmd));
    if (s_buttonCmdQueue == nullptr) {
        ESP_LOGE(TAG, "button cmd queue create failed");
        abort();
    }
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

void buttonSetAuthBlinkShortPressHandler(void (*fn)()) {
    s_authBlinkShortPressHandler = fn;
}
