#include "button.h"

#include "config.h"
#include "mqtt.h"

#include <Arduino.h>

#if defined(CORE_DEBUG_LEVEL) && CORE_DEBUG_LEVEL > 0
#define BUTTON_DBG_PRINTLN(x) Serial.println(x)
#else
#define BUTTON_DBG_PRINTLN(x) ((void)0)
#endif

static constexpr int kButtonLedPin = 4;

static constexpr unsigned long kDebounceStableMs = 20;

static constexpr unsigned long kLongPressMs = 5000;
static constexpr unsigned long kShortPressMinMs = 50;
static constexpr unsigned kPublishMaxAttempts = 2;
static constexpr unsigned long kPublishRetryDelayMs = 25;
static bool buttonHeldDown = false;
static unsigned long buttonPressStartMs = 0;
static bool longPressResetTriggered = false;

static int buttonLastRawReading = LOW;
static unsigned long buttonLastDebounceChangeMs = 0;
static int buttonDebouncedLevel = LOW;

#if defined(CORE_DEBUG_LEVEL) && CORE_DEBUG_LEVEL > 0
static int debugCounter = 0;
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
};

static LedTxPhase ledTxPhase = LedTxPhase::Idle;
static unsigned long ledPhaseStartMs = 0;
static unsigned long ledPhaseDurationMs = 0;
static unsigned publishFailCount = 0;

static void armLedPhase(unsigned long durationMs) {
    ledPhaseStartMs = millis();
    ledPhaseDurationMs = durationMs;
}

static bool ledSendSequenceActive() {
    return ledTxPhase != LedTxPhase::Idle;
}

bool buttonIsLedTxSequenceActive() {
    return ledSendSequenceActive();
}

static void startMqttSendLedSequence() {
    BUTTON_DBG_PRINTLN("Button-Druck erkannt!");
    BUTTON_DBG_PRINTLN("Sende MQTT-Nachricht (LED-Sequenz)...");
    ledTxPhase = LedTxPhase::PreOn1;
    digitalWrite(kButtonLedPin, HIGH);
    armLedPhase(100);
}

void buttonInit() {
    pinMode(kButtonGpio, INPUT_PULLDOWN);
    pinMode(kButtonLedPin, OUTPUT);
    buttonLastRawReading = digitalRead(kButtonGpio);
    buttonDebouncedLevel = buttonLastRawReading;
    buttonLastDebounceChangeMs = millis();
}

void buttonStartupBlink() {
    for (int i = 0; i < 3; i++) {
        digitalWrite(kButtonLedPin, HIGH);
        delay(200);
        digitalWrite(kButtonLedPin, LOW);
        delay(200);
    }
    digitalWrite(kButtonLedPin, LOW);
}

void checkLEDStatus() {
    if (ledTxPhase == LedTxPhase::Idle) {
        return;
    }

    const unsigned long now = millis();
    if (now - ledPhaseStartMs < ledPhaseDurationMs) {
        return;
    }

    switch (ledTxPhase) {
        case LedTxPhase::Idle:
            break;

        case LedTxPhase::PreOn1:
            digitalWrite(kButtonLedPin, LOW);
            ledTxPhase = LedTxPhase::PreOff1;
            armLedPhase(100);
            break;

        case LedTxPhase::PreOff1:
            digitalWrite(kButtonLedPin, HIGH);
            ledTxPhase = LedTxPhase::PreOn2;
            armLedPhase(100);
            break;

        case LedTxPhase::PreOn2:
            digitalWrite(kButtonLedPin, LOW);
            ledTxPhase = LedTxPhase::PreOff2;
            armLedPhase(100);
            break;

        case LedTxPhase::PreOff2:
            publishFailCount = 0;
            ledTxPhase = LedTxPhase::PublishTry;
            armLedPhase(0);
            break;

        case LedTxPhase::PublishTry: {
            const bool ok = mqttPublishHeart();
            if (ok) {
                BUTTON_DBG_PRINTLN("MQTT Nachricht erfolgreich gesendet!");
                ledTxPhase = LedTxPhase::PostWait;
                armLedPhase(500);
            } else {
                publishFailCount++;
                if (publishFailCount >= kPublishMaxAttempts) {
                    BUTTON_DBG_PRINTLN("MQTT Sendung fehlgeschlagen!");
                    digitalWrite(kButtonLedPin, LOW);
                    ledTxPhase = LedTxPhase::Idle;
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

        case LedTxPhase::PostWait:
            digitalWrite(kButtonLedPin, HIGH);
            ledTxPhase = LedTxPhase::PostOn1;
            armLedPhase(100);
            break;

        case LedTxPhase::PostOn1:
            digitalWrite(kButtonLedPin, LOW);
            ledTxPhase = LedTxPhase::PostOff1;
            armLedPhase(100);
            break;

        case LedTxPhase::PostOff1:
            digitalWrite(kButtonLedPin, HIGH);
            ledTxPhase = LedTxPhase::PostOn2;
            armLedPhase(100);
            break;

        case LedTxPhase::PostOn2:
            digitalWrite(kButtonLedPin, LOW);
            ledTxPhase = LedTxPhase::PostOff2;
            armLedPhase(100);
            break;

        case LedTxPhase::PostOff2:
            digitalWrite(kButtonLedPin, LOW);
            ledTxPhase = LedTxPhase::Idle;
            break;
    }
}

void buttonLoop() {
    const int raw = digitalRead(kButtonGpio);
    const unsigned long nowMs = millis();
    if (raw != buttonLastRawReading) {
        buttonLastRawReading = raw;
        buttonLastDebounceChangeMs = nowMs;
    }
    if (nowMs - buttonLastDebounceChangeMs >= kDebounceStableMs) {
        buttonDebouncedLevel = buttonLastRawReading;
    }
    const int reading = buttonDebouncedLevel;

    if (reading == HIGH) {
        if (!buttonHeldDown) {
            buttonHeldDown = true;
            buttonPressStartMs = nowMs;
            longPressResetTriggered = false;
        } else if (!longPressResetTriggered && (nowMs - buttonPressStartMs >= kLongPressMs)) {
            longPressResetTriggered = true;
            digitalWrite(kButtonLedPin, LOW);
            resetAllSettings();
        }
    } else {
        if (buttonHeldDown) {
            const unsigned long held = nowMs - buttonPressStartMs;
            if (!longPressResetTriggered && held >= kShortPressMinMs && held < kLongPressMs) {
                if (!ledSendSequenceActive()) {
                    startMqttSendLedSequence();
                }
            }
            buttonHeldDown = false;
            longPressResetTriggered = false;
        }
    }
}

void buttonDebugStatus() {
#if defined(CORE_DEBUG_LEVEL) && CORE_DEBUG_LEVEL > 0
    debugCounter++;
    Serial.println("=== DEBUG STATUS ===");
    Serial.print("Debug Counter: ");
    Serial.println(debugCounter);
    Serial.print("Button State: ");
    Serial.println(digitalRead(kButtonGpio));
    Serial.print("LED State: ");
    Serial.println(digitalRead(kButtonLedPin));
    Serial.println("==================");
#endif
}
