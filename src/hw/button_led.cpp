#include "button.h"

#include "button_config.h"
#include "button_internal.h"

#include "mqtt/mqtt.h"
#include "util/time_helpers.h"

#include <Arduino.h>
#include <cstdint>
#include <driver/gpio.h>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>

#include "util/log_tag.h"

DEFINE_LOG_TAG("BTN");

std::atomic<LedTxPhase> ledTxPhase{LedTxPhase::Idle};
unsigned long           ledPhaseStartMs    = 0;
unsigned long           ledPhaseDurationMs = 0;
unsigned                publishFailCount   = 0;

static std::atomic<bool>     s_refreshWanted{false};
static std::atomic<bool>     s_refreshHasDeadline{false};
static std::atomic<uint32_t> s_refreshDeadlineStartMs{0};
static std::atomic<uint32_t> s_refreshDeadlineDurMs{0};

static void notifyButtonTask() {
    const TaskHandle_t h = s_buttonTaskHandle.load(std::memory_order_acquire);
    if (h != nullptr) {
        xTaskNotifyGive(h);
    }
}

static bool ledIsRefreshPhase(LedTxPhase p) {
    return p == LedTxPhase::RefreshOn || p == LedTxPhase::RefreshOff;
}

static void startRefreshIfIdle() {
    if (ledTxPhase.load(std::memory_order_relaxed) == LedTxPhase::Idle) {
        ledTxPhase.store(LedTxPhase::RefreshOn, std::memory_order_relaxed);
        ledOutput(HIGH);
        armLedPhase(kLedRefreshPulseMs);
    }
}

void ledRefreshPulseBegin() {
    s_refreshHasDeadline.store(false, std::memory_order_relaxed);
    s_refreshWanted.store(true, std::memory_order_release);
    startRefreshIfIdle();
    notifyButtonTask();
}

void ledRefreshPulseEnd() {
    s_refreshWanted.store(false, std::memory_order_release);
    s_refreshHasDeadline.store(false, std::memory_order_relaxed);
    notifyButtonTask();
}

void ledRefreshPulseEndAfter(unsigned long durationMs) {
    s_refreshDeadlineStartMs.store(static_cast<uint32_t>(millis()), std::memory_order_relaxed);
    s_refreshDeadlineDurMs.store(static_cast<uint32_t>(durationMs), std::memory_order_relaxed);
    s_refreshHasDeadline.store(true, std::memory_order_release);
    notifyButtonTask();
}

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

bool ledActivityActive() {
    return ledTxPhase.load(std::memory_order_relaxed) != LedTxPhase::Idle;
}

bool ledTxBusy() {
    const LedTxPhase p = ledTxPhase.load(std::memory_order_relaxed);
    return p != LedTxPhase::Idle && !ledIsRefreshPhase(p);
}

bool ledSendSequenceActive() {
    return ledActivityActive();
}

bool buttonIsLedTxSequenceActive() {
    return ledActivityActive();
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
    if (s_refreshHasDeadline.load(std::memory_order_acquire)
        && deadlineReached(s_refreshDeadlineStartMs.load(std::memory_order_relaxed),
                           s_refreshDeadlineDurMs.load(std::memory_order_relaxed),
                           static_cast<uint32_t>(millis()))) {
        s_refreshWanted.store(false, std::memory_order_release);
        s_refreshHasDeadline.store(false, std::memory_order_relaxed);
    }

    if (ledTxPhase.load(std::memory_order_relaxed) == LedTxPhase::Idle) {
        if (s_refreshWanted.load(std::memory_order_acquire)) {
            startRefreshIfIdle();
        }
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
        if (s_refreshWanted.load(std::memory_order_acquire)) {
            ledTxPhase.store(LedTxPhase::RefreshOn, std::memory_order_relaxed);
            ledOutput(HIGH);
            armLedPhase(kLedRefreshPulseMs);
        } else {
            ledTxPhase.store(LedTxPhase::Idle, std::memory_order_relaxed);
            ledHoldWhenIdle();
        }
        break;

    case LedTxPhase::RefreshOn:
        if (!s_refreshWanted.load(std::memory_order_acquire)) {
            ledTxPhase.store(LedTxPhase::Idle, std::memory_order_relaxed);
            ledHoldWhenIdle();
            break;
        }
        ledOutput(LOW);
        ledTxPhase.store(LedTxPhase::RefreshOff, std::memory_order_relaxed);
        armLedPhase(kLedRefreshPulseMs);
        break;

    case LedTxPhase::RefreshOff:
        if (!s_refreshWanted.load(std::memory_order_acquire)) {
            ledTxPhase.store(LedTxPhase::Idle, std::memory_order_relaxed);
            ledHoldWhenIdle();
            break;
        }
        ledOutput(HIGH);
        ledTxPhase.store(LedTxPhase::RefreshOn, std::memory_order_relaxed);
        armLedPhase(kLedRefreshPulseMs);
        break;

    default:
        break;
    }
}
