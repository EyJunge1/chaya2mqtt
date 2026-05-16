#include "button.h"

#include "mqtt/config.h"
#include "pins.h"
#include "heart/counter.h"
#include "display/display.h"
#include "mqtt/mqtt.h"
#include "wifi/wlan.h"

#include <Arduino.h>
#include <atomic>
#include <climits>
#include <driver/gpio.h>
#include <esp_log.h>

#include "log_tag.h"

DEFINE_LOG_TAG("BTN");

static constexpr int kButtonLedPin = pins::kButtonLed;

static constexpr unsigned long kDebounceStableMs = 20;

/** Loslassen < 10 s: MQTT senden. Durchgehend >= 10 s halten: Factory Reset. */
static constexpr unsigned long kFactoryResetHoldMs = 10000;
static constexpr unsigned long kShortPressMinMs           = 50;
static constexpr unsigned kPublishMaxAttempts             = 2;
static constexpr unsigned long kPublishRetryDelayMs     = 25;
static constexpr unsigned long kFailFlashMs               = 50;

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

/** Nicht-blockierende MQTT-Sende-LED-Sequenz (2x Blink, Publish, Pause, 2x Blink). */
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
    /** 3x kurzes Flackern nach fehlgeschlagenem Publish (je kFailFlashMs an/aus). */
    FailOn1,
    FailOff1,
    FailOn2,
    FailOff2,
    FailOn3,
    FailOff3,
    AuthOn,
    AuthOff,
};

static LedTxPhase ledTxPhase = LedTxPhase::Idle;
static unsigned long ledPhaseStartMs     = 0;
static unsigned long ledPhaseDurationMs  = 0;
static unsigned publishFailCount         = 0;

static void armLedPhase(unsigned long durationMs) {
    ledPhaseStartMs    = millis();
    ledPhaseDurationMs = durationMs;
}

/** Vor jeder Aenderung: Hold loesen, damit digitalWrite wirkt; im Idle wieder halten. */
static void ledOutput(int level) {
    gpio_hold_dis(static_cast<gpio_num_t>(kButtonLedPin));
    digitalWrite(kButtonLedPin, level);
}

static void ledHoldWhenIdle() {
    gpio_hold_en(static_cast<gpio_num_t>(kButtonLedPin));
}

static void (*s_authBlinkShortPressHandler)() = nullptr;

static std::atomic<bool> s_authBlinkWantOnFromWeb{false};
static std::atomic<bool> s_authBlinkWantOffFromWeb{false};

void buttonRequestAuthBlinkOnFromAsync() {
    s_authBlinkWantOnFromWeb.store(true, std::memory_order_release);
}

void buttonRequestAuthBlinkOffFromAsync() {
    s_authBlinkWantOffFromWeb.store(true, std::memory_order_release);
}

static void consumeAuthBlinkRequestsFromWeb() {
    if (s_authBlinkWantOffFromWeb.exchange(false, std::memory_order_acq_rel)) {
        if (ledTxPhase == LedTxPhase::AuthOn || ledTxPhase == LedTxPhase::AuthOff) {
            ledOutput(LOW); /* off before gpio_hold freezes output */
            ledTxPhase = LedTxPhase::Idle;
            ledHoldWhenIdle();
        }
    }
    if (s_authBlinkWantOnFromWeb.exchange(false, std::memory_order_acq_rel)) {
        if (ledTxPhase == LedTxPhase::Idle) {
            ledTxPhase = LedTxPhase::AuthOn;
            ledOutput(HIGH);
            armLedPhase(500);
        }
    }
}

void buttonSetAuthBlinkShortPressHandler(void (*fn)()) {
    s_authBlinkShortPressHandler = fn;
}

void buttonSetAuthBlinkActive(bool active) {
    if (active) {
        if (ledTxPhase == LedTxPhase::Idle) {
            ledTxPhase = LedTxPhase::AuthOn;
            ledOutput(HIGH);
            armLedPhase(500);
        }
    } else {
        if (ledTxPhase == LedTxPhase::AuthOn || ledTxPhase == LedTxPhase::AuthOff) {
            ledOutput(LOW); /* off before gpio_hold freezes output */
            ledTxPhase = LedTxPhase::Idle;
            ledHoldWhenIdle();
        }
    }
}

bool buttonIsAuthBlinkActive() {
    return ledTxPhase == LedTxPhase::AuthOn || ledTxPhase == LedTxPhase::AuthOff;
}

static bool ledSendSequenceActive() {
    return ledTxPhase != LedTxPhase::Idle;
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

/** Einfache Phasen: LED setzen, naechste Phase, Dauer. */
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
    ledTxPhase = LedTxPhase::PreOn1;
    ledOutput(HIGH);
    armLedPhase(100);
}

void buttonInit() {
    pinMode(kButtonGpio, INPUT_PULLDOWN);
    pinMode(kButtonLedPin, OUTPUT);
    btn.lastRawReading       = digitalRead(kButtonGpio);
    btn.debouncedLevel       = btn.lastRawReading;
    btn.lastDebounceChangeMs = millis();
}

void buttonStartupBlink() {
    for (int i = 0; i < 3; i++) {
        ledOutput(HIGH);
        delay(200);
        ledOutput(LOW);
        delay(200);
    }
    ledOutput(LOW);
}

void buttonEnableLedGpioHoldForLightSleep() {
    ledHoldWhenIdle();
}

void buttonAdvanceLedSequence() {
    consumeAuthBlinkRequestsFromWeb();
    if (ledTxPhase == LedTxPhase::Idle) {
        return;
    }

    const unsigned long now = millis();
    if (now - ledPhaseStartMs < ledPhaseDurationMs) {
        return;
    }

    for (const LedPhaseRow& row : kLedPhaseRows) {
        if (ledTxPhase == row.from) {
            ledOutput(row.ledLevel);
            ledTxPhase = row.next;
            armLedPhase(row.durationMs);
            return;
        }
    }

    switch (ledTxPhase) {
        case LedTxPhase::PreOff2:
            publishFailCount = 0;
            ledTxPhase = LedTxPhase::PublishTry;
            armLedPhase(0);
            break;

        case LedTxPhase::PublishTry: {
            const bool ok = mqttPublishChaya();
            if (ok) {
                ESP_LOGI(TAG, "MQTT message published OK");
                if (heartSentCounter < INT_MAX) {
                    heartSentCounter++;
                }
                maybeSaveHeartSentCounter();
                requestHeartRedraw();
                ledTxPhase = LedTxPhase::PostWait;
                armLedPhase(500);
            } else {
                publishFailCount++;
                if (publishFailCount >= kPublishMaxAttempts) {
                    ESP_LOGW(TAG, "MQTT publish failed after retries");
                    ledOutput(HIGH);
                    ledTxPhase = LedTxPhase::FailOn1;
                    armLedPhase(kFailFlashMs);
                } else {
                    ledTxPhase = LedTxPhase::PublishRetryWait;
                    armLedPhase(kPublishRetryDelayMs);
                }
            }
            break;
        }

        case LedTxPhase::PublishRetryWait:
            ledTxPhase = LedTxPhase::PublishTry;
            armLedPhase(0);
            break;

        case LedTxPhase::PostOff2:  // NOLINT(bugprone-branch-clone)
        case LedTxPhase::FailOff3:
            ledTxPhase = LedTxPhase::Idle;
            ledHoldWhenIdle();
            break;

        default:
            break;
    }
}

/** Drei kurze Blinks (wechselnd HIGH/LOW, 6 Schritte) vor Factory-Reset, dann Neustart. */
static void triggerFactoryReset() {
    for (int i = 0; i < 6; i++) {
        ledOutput((i % 2) == 0 ? HIGH : LOW);
        delay(120);
    }
    ledOutput(LOW);
    resetAllSettings();
}

void buttonLoop() {
    consumeAuthBlinkRequestsFromWeb();
    const int raw            = digitalRead(kButtonGpio);
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
            btn.heldDown               = true;
            btn.pressStartMs           = nowMs;
            btn.factoryResetTriggered  = false;
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

#if defined(CORE_DEBUG_LEVEL) && CORE_DEBUG_LEVEL > 0
void buttonDebugStatus() {
    debugCounter++;
    ESP_LOGD(TAG, "Status #%u: Button=%d, LED=%d",
             debugCounter, digitalRead(kButtonGpio), digitalRead(kButtonLedPin));
}
#endif
