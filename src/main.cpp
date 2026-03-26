#include <Arduino.h>
#include <WiFi.h>

#include "button.h"
#include "config.h"
#include "display.h"
#include "mqtt.h"

void setup() {
    Serial.begin(115200);
    Serial.println("=== ESP32 Rotes Herz Display mit MQTT ===");

    displayInit();
    Serial.println("Display initialisiert...");

    buttonInit();

    loadMQTTConfig();
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

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi verloren! Versuche Reconnect...");
        WiFi.reconnect();
    }

    static unsigned long lastDbg = 0;
    if (millis() - lastDbg > 5000) {
        buttonDebugStatus();
        lastDbg = millis();
    }

    delay(5);
}
