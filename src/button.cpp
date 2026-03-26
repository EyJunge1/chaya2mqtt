#include "button.h"

#include "config.h"
#include "mqtt.h"

#include <Arduino.h>

#if defined(CORE_DEBUG_LEVEL) && CORE_DEBUG_LEVEL > 0
#define BUTTON_DBG_PRINTLN(x) Serial.println(x)
#else
#define BUTTON_DBG_PRINTLN(x) ((void)0)
#endif

static const int BUTTON_LED_PIN = 4;

static constexpr unsigned long kDebounceStableMs = 20;

static const unsigned long LONG_PRESS_MS = 5000;
static const unsigned long SHORT_PRESS_MIN_MS = 50;
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
    PostWait,
    PostOn1,
    PostOff1,
    PostOn2,
    PostOff2,
};

static LedTxPhase ledTxPhase = LedTxPhase::Idle;
static unsigned long ledPhaseStartMs = 0;
static unsigned long ledPhaseDurationMs = 0;

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
    digitalWrite(BUTTON_LED_PIN, HIGH);
    armLedPhase(100);
}

void buttonInit() {
    pinMode(kButtonGpio, INPUT_PULLDOWN);
    pinMode(BUTTON_LED_PIN, OUTPUT);
    buttonLastRawReading = digitalRead(kButtonGpio);
    buttonDebouncedLevel = buttonLastRawReading;
    buttonLastDebounceChangeMs = millis();
}

void buttonStartupBlink() {
    for (int i = 0; i < 3; i++) {
        digitalWrite(BUTTON_LED_PIN, HIGH);
        delay(200);
        digitalWrite(BUTTON_LED_PIN, LOW);
        delay(200);
    }
    digitalWrite(BUTTON_LED_PIN, LOW);
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
            digitalWrite(BUTTON_LED_PIN, LOW);
            ledTxPhase = LedTxPhase::PreOff1;
            armLedPhase(100);
            break;

        case LedTxPhase::PreOff1:
            digitalWrite(BUTTON_LED_PIN, HIGH);
            ledTxPhase = LedTxPhase::PreOn2;
            armLedPhase(100);
            break;

        case LedTxPhase::PreOn2:
            digitalWrite(BUTTON_LED_PIN, LOW);
            ledTxPhase = LedTxPhase::PreOff2;
            armLedPhase(100);
            break;

        case LedTxPhase::PreOff2:
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
                BUTTON_DBG_PRINTLN("MQTT Sendung fehlgeschlagen!");
                digitalWrite(BUTTON_LED_PIN, LOW);
                ledTxPhase = LedTxPhase::Idle;
            }
            break;
        }

        case LedTxPhase::PostWait:
            digitalWrite(BUTTON_LED_PIN, HIGH);
            ledTxPhase = LedTxPhase::PostOn1;
            armLedPhase(100);
            break;

        case LedTxPhase::PostOn1:
            digitalWrite(BUTTON_LED_PIN, LOW);
            ledTxPhase = LedTxPhase::PostOff1;
            armLedPhase(100);
            break;

        case LedTxPhase::PostOff1:
            digitalWrite(BUTTON_LED_PIN, HIGH);
            ledTxPhase = LedTxPhase::PostOn2;
            armLedPhase(100);
            break;

        case LedTxPhase::PostOn2:
            digitalWrite(BUTTON_LED_PIN, LOW);
            ledTxPhase = LedTxPhase::PostOff2;
            armLedPhase(100);
            break;

        case LedTxPhase::PostOff2:
            digitalWrite(BUTTON_LED_PIN, LOW);
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
        } else if (!longPressResetTriggered && (nowMs - buttonPressStartMs >= LONG_PRESS_MS)) {
            longPressResetTriggered = true;
            digitalWrite(BUTTON_LED_PIN, LOW);
            resetAllSettings();
        }
    } else {
        if (buttonHeldDown) {
            const unsigned long held = nowMs - buttonPressStartMs;
            if (!longPressResetTriggered && held >= SHORT_PRESS_MIN_MS && held < LONG_PRESS_MS) {
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
    Serial.println(digitalRead(BUTTON_LED_PIN));
    Serial.println("==================");
#endif
}
