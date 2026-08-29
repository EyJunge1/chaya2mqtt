#include "led.h"

#include "led_config.h"
#include "led_internal.h"
#include "led_pattern_pure.h"

#include "button/button.h"
#include "config/app_config.h"
#include "mqtt/mqtt.h"
#include "util/time_helpers.h"

#include <Arduino.h>
#include <cstdint>
#include <driver/gpio.h>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "util/log_tag.h"

DEFINE_LOG_TAG("LED");

std::atomic<LedTxPhase> ledTxPhase{LedTxPhase::Idle};
unsigned long           ledPhaseStartMs    = 0;
unsigned long           ledPhaseDurationMs = 0;

static std::atomic<bool>     s_refreshWanted{false};
static std::atomic<bool>     s_refreshHasDeadline{false};
static std::atomic<uint32_t> s_refreshDeadlineStartMs{0};
static std::atomic<uint32_t> s_refreshDeadlineDurMs{0};

static std::atomic<bool>     s_patternWanted{false};
static std::atomic<uint8_t>  s_patternCount{0};
static std::atomic<uint16_t> s_patternOnMs{0};
static std::atomic<uint16_t> s_patternOffMs{0};
static LedPatternRuntime     s_patternRt{};

static bool ledIsRefreshPhase(LedTxPhase p) {
    return p == LedTxPhase::RefreshOn || p == LedTxPhase::RefreshOff;
}

static bool ledIsPatternPhase(LedTxPhase p) {
    return p == LedTxPhase::PatternOn || p == LedTxPhase::PatternOff;
}

static bool startPatternFromQueue() {
    if (!s_patternWanted.load(std::memory_order_acquire)) {
        return false;
    }
    s_patternWanted.store(false, std::memory_order_release);
    uint8_t  count = s_patternCount.load(std::memory_order_relaxed);
    uint16_t onMs  = s_patternOnMs.load(std::memory_order_relaxed);
    uint16_t offMs = s_patternOffMs.load(std::memory_order_relaxed);
    if (!ledPatternBegin(s_patternRt, count, onMs, offMs)) {
        return false;
    }
    ledTxPhase.store(LedTxPhase::PatternOn, std::memory_order_relaxed);
    ledOutput(HIGH);
    armLedPhase(s_patternRt.onMs);
    return true;
}

static void startRefreshIfIdle() {
    if (ledTxPhase.load(std::memory_order_relaxed) == LedTxPhase::Idle) {
        ledTxPhase.store(LedTxPhase::RefreshOn, std::memory_order_relaxed);
        ledOutput(HIGH);
        armLedPhase(kLedRefreshPulseMs);
    }
}

static void startPatternIfAllowed() {
    const LedTxPhase p = ledTxPhase.load(std::memory_order_relaxed);
    if (p == LedTxPhase::Idle || ledIsRefreshPhase(p) || ledIsPatternPhase(p)) {
        (void)startPatternFromQueue();
    }
}

void ledRefreshPulseBegin() {
    s_refreshHasDeadline.store(false, std::memory_order_relaxed);
    s_refreshWanted.store(true, std::memory_order_release);
    startRefreshIfIdle();
    buttonNotifyTask();
}

void ledRefreshPulseEnd() {
    s_refreshWanted.store(false, std::memory_order_release);
    s_refreshHasDeadline.store(false, std::memory_order_relaxed);
    buttonNotifyTask();
}

void ledRefreshPulseEndAfter(unsigned long durationMs) {
    s_refreshDeadlineStartMs.store(static_cast<uint32_t>(millis()), std::memory_order_relaxed);
    s_refreshDeadlineDurMs.store(static_cast<uint32_t>(durationMs), std::memory_order_relaxed);
    s_refreshHasDeadline.store(true, std::memory_order_release);
    buttonNotifyTask();
}

void armLedPhase(unsigned long durationMs) {
    ledPhaseStartMs    = millis();
    ledPhaseDurationMs = durationMs;
}

void ledOutputForced(int level) {
    // Header user LED is active-low: HIGH in the state machine means "on".
    gpio_hold_dis(static_cast<gpio_num_t>(kButtonLedPin));
    digitalWrite(kButtonLedPin, level == HIGH ? LOW : HIGH);
}

void ledOutput(int level) {
    if (!configGetLedEnabled()) {
        ledOutputForced(LOW);
        return;
    }
    ledOutputForced(level);
}

void ledInit() {
    pinMode(kButtonLedPin, OUTPUT);
    ledOutput(LOW);  // active-low LED off
}

void ledApplyEnabled() {
    if (!configGetLedEnabled()) {
        ledOutputForced(LOW);
        ledHoldWhenIdle();
    }
}

void ledEnableGpioHoldForLightSleep() {
    ledHoldWhenIdle();
}

void ledHoldWhenIdle() {
    gpio_hold_en(static_cast<gpio_num_t>(kButtonLedPin));
}

bool ledActivityActive() {
    return ledTxPhase.load(std::memory_order_relaxed) != LedTxPhase::Idle;
}

bool ledIsActivityActive() {
    return ledActivityActive();
}

bool ledTxBusy() {
    const LedTxPhase p = ledTxPhase.load(std::memory_order_relaxed);
    return p != LedTxPhase::Idle && !ledIsRefreshPhase(p) && !ledIsPatternPhase(p);
}

bool ledIsTxSendBusy() {
    return ledTxBusy();
}

bool ledSendSequenceActive() {
    return ledActivityActive();
}

static LedBlinkPattern ledPresetToPattern(LedPreset preset) {
    switch (preset) {
    case LedPreset::Boot:
        return {kLedPresetBootCount, kLedPresetBootOnMs, kLedPresetBootOffMs};
    case LedPreset::WifiUp:
        return {kLedPresetWifiUpCount, kLedPresetWifiUpOnMs, kLedPresetWifiUpOffMs};
    case LedPreset::MqttUp:
        return {kLedPresetMqttUpCount, kLedPresetMqttUpOnMs, kLedPresetMqttUpOffMs};
    case LedPreset::LinkDown:
        return {kLedPresetLinkDownCount, kLedPresetLinkDownOnMs, kLedPresetLinkDownOffMs};
    case LedPreset::SoftOff:
        return {kLedPresetSoftOffCount, kLedPresetSoftOffOnMs, kLedPresetSoftOffOffMs};
    }
    return {kLedPresetBootCount, kLedPresetBootOnMs, kLedPresetBootOffMs};
}

void ledPlayPattern(LedBlinkPattern pattern) {
    ledPatternNormalize(pattern.count, pattern.onMs, pattern.offMs);
    s_patternCount.store(pattern.count, std::memory_order_relaxed);
    s_patternOnMs.store(pattern.onMs, std::memory_order_relaxed);
    s_patternOffMs.store(pattern.offMs, std::memory_order_relaxed);
    s_patternWanted.store(true, std::memory_order_release);
    startPatternIfAllowed();
    buttonNotifyTask();
}

void ledPlayPreset(LedPreset preset) {
    ledPlayPattern(ledPresetToPattern(preset));
}

void ledPlayPatternBlocking(LedBlinkPattern pattern) {
    if (!configGetLedEnabled()) {
        ledOutput(LOW);
        return;
    }
    ledPatternNormalize(pattern.count, pattern.onMs, pattern.offMs);
    for (uint8_t i = 0; i < pattern.count; i++) {
        ledOutput(HIGH);
        vTaskDelay(pdMS_TO_TICKS(pattern.onMs));
        ledOutput(LOW);
        if (pattern.offMs > 0) {
            vTaskDelay(pdMS_TO_TICKS(pattern.offMs));
        }
    }
}

void ledPlayPresetBlocking(LedPreset preset) {
    ledPlayPatternBlocking(ledPresetToPattern(preset));
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
    ESP_LOGI(TAG, "Chaya send: MQTT TX LED sequence");
    s_patternWanted.store(false, std::memory_order_release);
    ledTxPhase.store(LedTxPhase::PreOn1, std::memory_order_relaxed);
    ledOutput(HIGH);
    armLedPhase(kLedSequenceStepMs);
}

void ledStartChayaSendSequence() {
    startMqttSendLedSequence();
    buttonNotifyTask();
}

static void finishToIdleOrBackground() {
    if (startPatternFromQueue()) {
        return;
    }
    if (s_refreshWanted.load(std::memory_order_acquire)) {
        ledTxPhase.store(LedTxPhase::RefreshOn, std::memory_order_relaxed);
        ledOutput(HIGH);
        armLedPhase(kLedRefreshPulseMs);
        return;
    }
    ledTxPhase.store(LedTxPhase::Idle, std::memory_order_relaxed);
    ledHoldWhenIdle();
}

void advanceLedSequence() {
    if (s_refreshHasDeadline.load(std::memory_order_acquire)
        && deadlineReached(s_refreshDeadlineStartMs.load(std::memory_order_relaxed),
                           s_refreshDeadlineDurMs.load(std::memory_order_relaxed),
                           static_cast<uint32_t>(millis()))) {
        s_refreshWanted.store(false, std::memory_order_release);
        s_refreshHasDeadline.store(false, std::memory_order_relaxed);
    }

    const LedTxPhase phaseNow = ledTxPhase.load(std::memory_order_relaxed);

    if (ledIsPatternPhase(phaseNow) && s_patternWanted.load(std::memory_order_acquire)) {
        (void)startPatternFromQueue();
        return;
    }

    if (ledIsRefreshPhase(phaseNow) && s_patternWanted.load(std::memory_order_acquire)) {
        (void)startPatternFromQueue();
        return;
    }

    if (phaseNow == LedTxPhase::Idle) {
        if (startPatternFromQueue()) {
            return;
        }
        if (s_refreshWanted.load(std::memory_order_acquire)) {
            startRefreshIfIdle();
        }
        return;
    }

    const unsigned long now = millis();
    if (now - ledPhaseStartMs < ledPhaseDurationMs) {
        return;
    }

    if (ledIsPatternPhase(phaseNow)) {
        const LedPatternAdvanceResult r = ledPatternAdvance(s_patternRt);
        if (r.done) {
            finishToIdleOrBackground();
            return;
        }
        ledOutput(r.ledOn ? HIGH : LOW);
        ledTxPhase.store(r.ledOn ? LedTxPhase::PatternOn : LedTxPhase::PatternOff,
                         std::memory_order_relaxed);
        armLedPhase(r.durationMs);
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
        ledTxPhase.store(LedTxPhase::PublishTry, std::memory_order_relaxed);
        armLedPhase(0);
        break;

    case LedTxPhase::PublishTry: {
        const MqttChayaPublishAsync st = mqttRequestChayaPublishAsync();
        if (st == MqttChayaPublishAsync::Pending || st == MqttChayaPublishAsync::Idle) {
            // Stay in PublishTry; short poll so button/PWR keep running (STAB-02).
            armLedPhase(50);
            break;
        }
        mqttClearChayaPublishAsync();
        if (st == MqttChayaPublishAsync::Ok) {
            ESP_LOGI(TAG, "MQTT message acknowledged by broker");
            ledTxPhase.store(LedTxPhase::PostWait, std::memory_order_relaxed);
            armLedPhase(kPostPublishWaitMs);
        } else {
            ESP_LOGW(TAG, "MQTT publish was not acknowledged");
            ledOutput(HIGH);
            ledTxPhase.store(LedTxPhase::FailOn1, std::memory_order_relaxed);
            armLedPhase(kFailFlashMs);
        }
        break;
    }

    case LedTxPhase::PostOff2:
    case LedTxPhase::FailOff3:
        finishToIdleOrBackground();
        break;

    case LedTxPhase::RefreshOn:
        if (s_patternWanted.load(std::memory_order_acquire)) {
            (void)startPatternFromQueue();
            break;
        }
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
        if (s_patternWanted.load(std::memory_order_acquire)) {
            (void)startPatternFromQueue();
            break;
        }
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
