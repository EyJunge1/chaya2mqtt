#include "button.h"

#include "config.h"
#include "display.h"
#include "mqtt.h"

#include <Arduino.h>

static const int BUTTON_PIN = 2;
static const int BUTTON_LED_PIN = 4;

static unsigned long ledOnTime = 0;
static unsigned long ledDuration = 0;
static bool ledActive = false;

static const unsigned long LONG_PRESS_MS = 5000;
static const unsigned long SHORT_PRESS_MIN_MS = 50;
static bool buttonHeldDown = false;
static unsigned long buttonPressStartMs = 0;
static bool longPressResetTriggered = false;

static unsigned long lastDebugTime = 0;
static int debugCounter = 0;

static void blinkLEDTwice() {
    Serial.println("LED blinkt 2x...");
    digitalWrite(BUTTON_LED_PIN, HIGH);
    delay(100);
    digitalWrite(BUTTON_LED_PIN, LOW);
    delay(100);
    digitalWrite(BUTTON_LED_PIN, HIGH);
    delay(100);
    digitalWrite(BUTTON_LED_PIN, LOW);
}

static void handleButtonPress() {
    Serial.println("Button-Druck erkannt!");
    Serial.println("Sende MQTT-Nachricht...");

    blinkLEDTwice();

    if (client.connected()) {
        String message = String(counter);
        if (client.publish(mqtt_topic_pub, message.c_str())) {
            Serial.println("MQTT Nachricht erfolgreich gesendet!");
            delay(500);
            blinkLEDTwice();
        } else {
            Serial.println("MQTT Sendung fehlgeschlagen!");
        }
    } else {
        Serial.println("MQTT nicht verbunden!");
    }
}

void buttonInit() {
    pinMode(BUTTON_PIN, INPUT);
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
    if (ledActive) {
        unsigned long elapsed = millis() - ledOnTime;
        if (elapsed >= ledDuration) {
            digitalWrite(BUTTON_LED_PIN, LOW);
            ledActive = false;
        }
    }
}

void buttonLoop() {
    int reading = digitalRead(BUTTON_PIN);

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
            unsigned long held = millis() - buttonPressStartMs;
            if (!longPressResetTriggered && held >= SHORT_PRESS_MIN_MS && held < LONG_PRESS_MS) {
                handleButtonPress();
            }
            buttonHeldDown = false;
            longPressResetTriggered = false;
        }
    }
}

void buttonDebugStatus() {
    if (millis() - lastDebugTime > 5000) {
        lastDebugTime = millis();
        debugCounter++;
        Serial.println("=== DEBUG STATUS ===");
        Serial.print("Debug Counter: ");
        Serial.println(debugCounter);
        Serial.print("Button State: ");
        Serial.println(digitalRead(BUTTON_PIN));
        Serial.print("LED State: ");
        Serial.println(digitalRead(BUTTON_LED_PIN));
        Serial.println("==================");
    }
}
