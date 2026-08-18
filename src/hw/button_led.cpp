#include "button.h"

#include "button_config.h"
#include "button_internal.h"

#include "mqtt/mqtt.h"

#include <Arduino.h>
#include <driver/gpio.h>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>

#include "util/log_tag.h"

DEFINE_LOG_TAG("BTN");

std::atomic<LedTxPhase> ledTxPhase{LedTxPhase::Idle};
unsigned long           ledPhaseStartMs    = 0;
unsigned long           ledPhaseDurationMs = 0;
unsigned                publishFailCount   = 0;

void armLedPhase(unsigned long durationMs) {
    ledPhaseStartMs    = millis();
    ledPhaseDurationMs = durationMs;
}

void ledOutput(int level) {
    // Header user LED is active-low: HIGH in the state machine means "on".
    gpio_hold_dis(static_cast<gpio_num_t>(kButtonLedPin));
    digitalWrite(kButtonLedPin, level == HIGH ? LOW : HIGH);
}

void ledHoldWhenIdle() {
    gpio_hold_en(static_cast<gpio_num_t>(kButtonLedPin));
}

bool ledSendSequenceActive() {
    return ledTxPhase.load(std::memory_order_relaxed) != LedTxPhase::Idle;
}

bool buttonIsLedTxSequenceActive() {
    return ledSendSequenceActive();
}

struct LedPhaseRow {
    LedTxPhase    from;
    int           ledLevel;
    LedTxPhase    next;
    unsigned long durationMs;
};

static constexpr LedPhaseRow kLedPhaseRows[] = {
    {LedTxPhase::PreOn1, LOW, LedTxPhase::PreOff1, kLedSequenceStepMs},
    {LedTxPhase::PreOff1, HIGH, LedTxPhase::PreOn2, kLedSequenceStepMs},
    {LedTxPhase::PreOn2, LOW, LedTxPhase::PreOff2, kLedSequenceStepMs},
    {LedTxPhase::PostWait, HIGH, LedTxPhase::PostOn1, kLedSequenceStepMs},
    {LedTxPhase::PostOn1, LOW, LedTxPhase::PostOff1, kLedSequenceStepMs},
    {LedTxPhase::PostOff1, HIGH, LedTxPhase::PostOn2, kLedSequenceStepMs},
    {LedTxPhase::PostOn2, LOW, LedTxPhase::PostOff2, kLedSequenceStepMs},
    {LedTxPhase::FailOn1, LOW, LedTxPhase::FailOff1, kFailFlashMs},
    {LedTxPhase::FailOff1, HIGH, LedTxPhase::FailOn2, kFailFlashMs},
    {LedTxPhase::FailOn2, LOW, LedTxPhase::FailOff2, kFailFlashMs},
    {LedTxPhase::FailOff2, HIGH, LedTxPhase::FailOn3, kFailFlashMs},
    {LedTxPhase::FailOn3, LOW, LedTxPhase::FailOff3, kFailFlashMs},
};

void startMqttSendLedSequence() {
    ESP_LOGI(TAG, "Button press: publishing MQTT (LED sequence)");
    ledTxPhase.store(LedTxPhase::PreOn1, std::memory_order_relaxed);
    ledOutput(HIGH);
    armLedPhase(kLedSequenceStepMs);
}

void advanceLedSequence() {
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
            armLedPhase(kPostPublishWaitMs);
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
