#include "button.h"

#include "mqtt/config.h"
#include "pins.h"
#include "display/display.h"
#include "mqtt/mqtt.h"
#include "wifi/wlan.h"

#include "diag/stack_monitor.h"
#include "diag/task_watchdog.h"

#include <atomic>
#include <cstdint>
#include <cstdlib>

#include <Arduino.h>
#include <driver/gpio.h>
#include <esp_err.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "log_tag.h"

DEFINE_LOG_TAG("BTN");

// Button debounce + LED: MQTT TX feedback, web-auth blink, 10s factory reset. ISR → task notify.

static constexpr int kButtonLedPin = pins::kButtonLed;

static constexpr unsigned long kDebounceStableMs = 20;
static constexpr unsigned long kFactoryResetHoldMs = 10000;
static constexpr unsigned long kShortPressMinMs           = 50;
static constexpr unsigned kPublishMaxAttempts             = 2;
static constexpr unsigned long kPublishRetryDelayMs     = 25;
static constexpr unsigned long kFailFlashMs               = 50;

static std::atomic<TaskHandle_t> s_buttonTaskHandle{nullptr};

static struct {
    bool           heldDown               = false;
    unsigned long  pressStartMs           = 0;
    bool           factoryResetTriggered  = false;
    int            lastRawReading         = LOW;
    unsigned long  lastDebounceChangeMs   = 0;
    int            debouncedLevel         = LOW;
} btn;

#if defined(CORE_DEBUG_LEVEL) && CORE_DEBUG_LEVEL > 0
static unsigned debugCounter = 0;
#endif

enum class LedTxPhase : uint8_t {
    Idle,
    PreOn1,
    PreOff1,
    PreOn2,
    PreOff2,
    PublishTry,
    PublishRetryWait,
    PostWait,
    PostOn1,
    PostOff1,
    PostOn2,
    PostOff2,
    FailOn1,
    FailOff1,
    FailOn2,
    FailOff2,
    FailOn3,
    FailOff3,
    AuthOn,
    AuthOff,
};

static std::atomic<LedTxPhase> ledTxPhase{LedTxPhase::Idle};
static unsigned long ledPhaseStartMs     = 0;
static unsigned long ledPhaseDurationMs  = 0;
static unsigned publishFailCount         = 0;

static void armLedPhase(unsigned long durationMs) {
    ledPhaseStartMs    = millis();
    ledPhaseDurationMs = durationMs;
}

static void ledOutput(int level) {
    gpio_hold_dis(static_cast<gpio_num_t>(kButtonLedPin));
    digitalWrite(kButtonLedPin, level);
}

static void ledHoldWhenIdle() {
    gpio_hold_en(static_cast<gpio_num_t>(kButtonLedPin));
}

static void (*s_authBlinkShortPressHandler)() = nullptr;

enum class ButtonInternalCmd : uint8_t {
    AuthBlinkOn,
    AuthBlinkOff,
};

static QueueHandle_t s_buttonCmdQueue = nullptr;

void buttonRequestAuthBlinkOnFromAsync() {
    ButtonInternalCmd cmd = ButtonInternalCmd::AuthBlinkOn;
    if (s_buttonCmdQueue != nullptr && xQueueSend(s_buttonCmdQueue, &cmd, 0) != pdTRUE) {
        ESP_LOGW(TAG, "button cmd queue full (AuthBlinkOn)");
    }
}

void buttonRequestAuthBlinkOffFromAsync() {
    ButtonInternalCmd cmd = ButtonInternalCmd::AuthBlinkOff;
    if (s_buttonCmdQueue != nullptr && xQueueSend(s_buttonCmdQueue, &cmd, 0) != pdTRUE) {
        ESP_LOGW(TAG, "button cmd queue full (AuthBlinkOff)");
    }
}

static void consumeButtonCommands() {
    ButtonInternalCmd cmd;
    while (xQueueReceive(s_buttonCmdQueue, &cmd, 0) == pdTRUE) {
        switch (cmd) {
        case ButtonInternalCmd::AuthBlinkOff: {
            const LedTxPhase ph = ledTxPhase.load(std::memory_order_relaxed);
            if (ph == LedTxPhase::AuthOn || ph == LedTxPhase::AuthOff) {
                ledOutput(LOW);
                ledTxPhase.store(LedTxPhase::Idle, std::memory_order_relaxed);
                ledHoldWhenIdle();
            }
            break;
        }
        case ButtonInternalCmd::AuthBlinkOn:
            if (ledTxPhase.load(std::memory_order_relaxed) == LedTxPhase::Idle) {
                ledTxPhase.store(LedTxPhase::AuthOn, std::memory_order_relaxed);
                ledOutput(HIGH);
                armLedPhase(500);
            }
            break;
        }
    }
}

void buttonSetAuthBlinkShortPressHandler(void (*fn)()) {
    s_authBlinkShortPressHandler = fn;
}

void buttonSetAuthBlinkActive(bool active) {
    if (active) {
        ButtonInternalCmd cmd = ButtonInternalCmd::AuthBlinkOn;
        if (s_buttonCmdQueue != nullptr && xQueueSend(s_buttonCmdQueue, &cmd, 0) != pdTRUE) {
            ESP_LOGW(TAG, "button cmd queue full (SetAuthBlink On)");
        }
    } else {
        ButtonInternalCmd cmd = ButtonInternalCmd::AuthBlinkOff;
        if (s_buttonCmdQueue != nullptr && xQueueSend(s_buttonCmdQueue, &cmd, 0) != pdTRUE) {
            ESP_LOGW(TAG, "button cmd queue full (SetAuthBlink Off)");
        }
    }
}

bool buttonIsAuthBlinkActive() {
    const LedTxPhase ph = ledTxPhase.load(std::memory_order_relaxed);
    return ph == LedTxPhase::AuthOn || ph == LedTxPhase::AuthOff;
}

static bool ledSendSequenceActive() {
    return ledTxPhase.load(std::memory_order_relaxed) != LedTxPhase::Idle;
}

bool buttonIsLedTxSequenceActive() {
    return ledSendSequenceActive();
}

struct LedPhaseRow {
    LedTxPhase      from;
    int             ledLevel;
    LedTxPhase      next;
    unsigned long   durationMs;
};

static constexpr LedPhaseRow kLedPhaseRows[] = {
    {LedTxPhase::PreOn1, LOW, LedTxPhase::PreOff1, 100},
    {LedTxPhase::PreOff1, HIGH, LedTxPhase::PreOn2, 100},
    {LedTxPhase::PreOn2, LOW, LedTxPhase::PreOff2, 100},
    {LedTxPhase::PostWait, HIGH, LedTxPhase::PostOn1, 100},
    {LedTxPhase::PostOn1, LOW, LedTxPhase::PostOff1, 100},
    {LedTxPhase::PostOff1, HIGH, LedTxPhase::PostOn2, 100},
    {LedTxPhase::PostOn2, LOW, LedTxPhase::PostOff2, 100},
    {LedTxPhase::FailOn1, LOW, LedTxPhase::FailOff1, kFailFlashMs},
    {LedTxPhase::FailOff1, HIGH, LedTxPhase::FailOn2, kFailFlashMs},
    {LedTxPhase::FailOn2, LOW, LedTxPhase::FailOff2, kFailFlashMs},
    {LedTxPhase::FailOff2, HIGH, LedTxPhase::FailOn3, kFailFlashMs},
    {LedTxPhase::FailOn3, LOW, LedTxPhase::FailOff3, kFailFlashMs},
    {LedTxPhase::AuthOn, LOW, LedTxPhase::AuthOff, 500},
    {LedTxPhase::AuthOff, HIGH, LedTxPhase::AuthOn, 500},
};

static void startMqttSendLedSequence() {
    ESP_LOGI(TAG, "Button press: publishing MQTT (LED sequence)");
    ledTxPhase.store(LedTxPhase::PreOn1, std::memory_order_relaxed);
    ledOutput(HIGH);
    armLedPhase(100);
}

static void advanceLedSequence() {
    if (ledTxPhase.load(std::memory_order_relaxed) == LedTxPhase::Idle) {
        return;
    }

    const unsigned long now = millis();
    if (now - ledPhaseStartMs < ledPhaseDurationMs) {
        return;
    }

    for (const LedPhaseRow& row : kLedPhaseRows) {
        if (ledTxPhase.load(std::memory_order_relaxed) == row.from) {
            ledOutput(row.ledLevel);
            ledTxPhase.store(row.next, std::memory_order_relaxed);
            armLedPhase(row.durationMs);
            return;
        }
    }

    switch (ledTxPhase.load(std::memory_order_relaxed)) {
        case LedTxPhase::PreOff2:
            publishFailCount = 0;
            ledTxPhase.store(LedTxPhase::PublishTry, std::memory_order_relaxed);
            armLedPhase(0);
            break;

        case LedTxPhase::PublishTry: {
            const bool ok = mqttPublishChayaAndApplySentCounters();
            if (ok) {
                ESP_LOGI(TAG, "MQTT message published OK");
                ledTxPhase.store(LedTxPhase::PostWait, std::memory_order_relaxed);
                armLedPhase(500);
            } else {
                publishFailCount++;
                if (publishFailCount >= kPublishMaxAttempts) {
                    ESP_LOGW(TAG, "MQTT publish failed after retries");
                    ledOutput(HIGH);
                    ledTxPhase.store(LedTxPhase::FailOn1, std::memory_order_relaxed);
                    armLedPhase(kFailFlashMs);
                } else {
                    ledTxPhase.store(LedTxPhase::PublishRetryWait, std::memory_order_relaxed);
                    armLedPhase(kPublishRetryDelayMs);
                }
            }
            break;
        }

        case LedTxPhase::PublishRetryWait:
            ledTxPhase.store(LedTxPhase::PublishTry, std::memory_order_relaxed);
            armLedPhase(0);
            break;

        case LedTxPhase::PostOff2:
        case LedTxPhase::FailOff3:
            ledTxPhase.store(LedTxPhase::Idle, std::memory_order_relaxed);
            ledHoldWhenIdle();
            break;

        default:
            break;
    }
}

static void triggerFactoryReset() {
    for (int i = 0; i < 6; i++) {
        ledOutput((i % 2) == 0 ? HIGH : LOW);
        vTaskDelay(pdMS_TO_TICKS(120));
    }
    ledOutput(LOW);
    resetAllSettings();
}

static void buttonPollAndProcess() {
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
                    MqttConfig btnMqttCfg{};
                    mqttCfgSnapshot(&btnMqttCfg);
                    if (btnMqttCfg.server[0] != '\0') {
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
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10));
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
        xTaskCreatePinnedToCore(buttonTaskFn, "button", 4096, nullptr, 8, &th, 1);
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
    for (int i = 0; i < 3; i++) {
        ledOutput(HIGH);
        vTaskDelay(pdMS_TO_TICKS(200));
        ledOutput(LOW);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

void buttonEnableLedGpioHoldForLightSleep() {
    ledHoldWhenIdle();
}

#if defined(CORE_DEBUG_LEVEL) && CORE_DEBUG_LEVEL > 0
void buttonDebugStatus() {
    debugCounter++;
    ESP_LOGD(TAG, "Status #%u: Button=%d, LED=%d",
             debugCounter, digitalRead(kButtonGpio), digitalRead(kButtonLedPin));
}
#endif
