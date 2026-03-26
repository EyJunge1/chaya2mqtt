#include <Arduino.h>
#include <WiFi.h>

#include "button.h"
#include "config.h"
#include "display.h"
#include "mqtt.h"

static constexpr unsigned long kWifiReconnectIntervalMs = 30000;

void setup() {
    Serial.begin(115200);
    Serial.println("=== ESP32 Rotes Herz Display mit MQTT ===");

    displayInit();
    Serial.println("Display initialisiert...");

    buttonInit();

    loadMQTTConfig();
    loadHeartCounter();
    setupWiFi();

    mqttSetup();

    Serial.println("Setup abgeschlossen");

    drawHeartWithNumber();

    buttonStartupBlink();
}

void loop() {
    buttonLoop();
    checkLEDStatus();
    mqttLoop();
    maybeSaveHeartCounter();

    static unsigned long lastWifiReconnectMs = 0;
    if (WiFi.status() != WL_CONNECTED) { // NOLINT(readability-static-accessed-through-instance)
        const unsigned long now = millis();
        if (now - lastWifiReconnectMs >= kWifiReconnectIntervalMs) {
            lastWifiReconnectMs = now;
            Serial.println("WiFi verloren! Versuche Reconnect...");
            WiFi.reconnect();
        }
    } else {
        lastWifiReconnectMs = millis();
    }

    if (consumeHeartRedraw()) {
        drawHeartWithNumber();
    }

    static unsigned long lastDbg = 0;
    if (millis() - lastDbg > 5000) {
        buttonDebugStatus();
        lastDbg = millis();
    }

    delay(5);
}
