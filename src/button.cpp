#include "button.h"

#include "config.h"
#include "mqtt.h"

#include <Arduino.h>
#include <cstdio>

static const int BUTTON_PIN = 2;
static const int BUTTON_LED_PIN = 4;

static const unsigned long LONG_PRESS_MS = 5000;
static const unsigned long SHORT_PRESS_MIN_MS = 50;
static bool buttonHeldDown = false;
static unsigned long buttonPressStartMs = 0;
static bool longPressResetTriggered = false;

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
static unsigned long ledPhaseUntilMs = 0;

static bool ledSendSequenceActive() {
    return ledTxPhase != LedTxPhase::Idle;
}

static void startMqttSendLedSequence() {
    Serial.println("Button-Druck erkannt!");
    Serial.println("Sende MQTT-Nachricht (LED-Sequenz)...");
    ledTxPhase = LedTxPhase::PreOn1;
    digitalWrite(BUTTON_LED_PIN, HIGH);
    ledPhaseUntilMs = millis() + 100;
}

void buttonInit() {
    pinMode(BUTTON_PIN, INPUT_PULLDOWN);
    pinMode(BUTTON_LED_PIN, OUTPUT);
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
    if (now < ledPhaseUntilMs) {
        return;
    }

    switch (ledTxPhase) {
        case LedTxPhase::Idle:
            break;

        case LedTxPhase::PreOn1:
            digitalWrite(BUTTON_LED_PIN, LOW);
            ledTxPhase = LedTxPhase::PreOff1;
            ledPhaseUntilMs = now + 100;
            break;

        case LedTxPhase::PreOff1:
            digitalWrite(BUTTON_LED_PIN, HIGH);
            ledTxPhase = LedTxPhase::PreOn2;
            ledPhaseUntilMs = now + 100;
            break;

        case LedTxPhase::PreOn2:
            digitalWrite(BUTTON_LED_PIN, LOW);
            ledTxPhase = LedTxPhase::PreOff2;
            ledPhaseUntilMs = now + 100;
            break;

        case LedTxPhase::PreOff2:
            ledTxPhase = LedTxPhase::PublishTry;
            ledPhaseUntilMs = now;
            break;

        case LedTxPhase::PublishTry: {
            bool ok = false;
            if (client.connected()) {
                char message[16];
                snprintf(message, sizeof(message), "%d", counter);
                ok = client.publish(mqtt_topic_pub, message);
            } else {
                Serial.println("MQTT nicht verbunden!");
            }
            if (ok) {
                Serial.println("MQTT Nachricht erfolgreich gesendet!");
                ledTxPhase = LedTxPhase::PostWait;
                ledPhaseUntilMs = now + 500;
            } else {
                Serial.println("MQTT Sendung fehlgeschlagen!");
                digitalWrite(BUTTON_LED_PIN, LOW);
                ledTxPhase = LedTxPhase::Idle;
            }
            break;
        }

        case LedTxPhase::PostWait:
            digitalWrite(BUTTON_LED_PIN, HIGH);
            ledTxPhase = LedTxPhase::PostOn1;
            ledPhaseUntilMs = now + 100;
            break;

        case LedTxPhase::PostOn1:
            digitalWrite(BUTTON_LED_PIN, LOW);
            ledTxPhase = LedTxPhase::PostOff1;
            ledPhaseUntilMs = now + 100;
            break;

        case LedTxPhase::PostOff1:
            digitalWrite(BUTTON_LED_PIN, HIGH);
            ledTxPhase = LedTxPhase::PostOn2;
            ledPhaseUntilMs = now + 100;
            break;

        case LedTxPhase::PostOn2:
            digitalWrite(BUTTON_LED_PIN, LOW);
            ledTxPhase = LedTxPhase::PostOff2;
            ledPhaseUntilMs = now + 100;
            break;

        case LedTxPhase::PostOff2:
            digitalWrite(BUTTON_LED_PIN, LOW);
            ledTxPhase = LedTxPhase::Idle;
            break;
    }
}

void buttonLoop() {
    const int reading = digitalRead(BUTTON_PIN);

    if (reading == HIGH) {
        if (!buttonHeldDown) {
            buttonHeldDown = true;
            buttonPressStartMs = millis();
            longPressResetTriggered = false;
        } else if (!longPressResetTriggered && (millis() - buttonPressStartMs >= LONG_PRESS_MS)) {
            longPressResetTriggered = true;
            resetAllSettings();
        }
    } else {
        if (buttonHeldDown) {
            const unsigned long held = millis() - buttonPressStartMs;
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
    Serial.println(digitalRead(BUTTON_PIN));
    Serial.print("LED State: ");
    Serial.println(digitalRead(BUTTON_LED_PIN));
    Serial.println("==================");
#endif
}
