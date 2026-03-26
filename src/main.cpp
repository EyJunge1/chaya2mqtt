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
    const unsigned long now = millis();

    buttonLoop();
    checkLEDStatus();
    mqttLoop();
    maybeSaveHeartCounter();

    static unsigned long lastWifiReconnectMs = 0;
    static bool wifiWasConnected = true;
    static bool wifiReconnectDueImmediately = false;
    if (WiFi.status() != WL_CONNECTED) { // NOLINT(readability-static-accessed-through-instance)
        if (wifiWasConnected) {
            wifiReconnectDueImmediately = true;
            wifiWasConnected = false;
        }
        if (wifiReconnectDueImmediately || now - lastWifiReconnectMs >= kWifiReconnectIntervalMs) {
            wifiReconnectDueImmediately = false;
            lastWifiReconnectMs = now;
            Serial.println("WiFi verloren! Versuche Reconnect...");
            WiFi.reconnect();
        }
    } else {
        wifiWasConnected = true;
        lastWifiReconnectMs = now;
    }

    if (consumeHeartRedraw()) {
        drawHeartWithNumber();
        flushHeartCounterIfDirty();
    }

#if defined(CORE_DEBUG_LEVEL) && CORE_DEBUG_LEVEL > 0
    static unsigned long lastDbg = 0;
    if (now - lastDbg > 5000) {
        buttonDebugStatus();
        lastDbg = now;
    }
#endif

    delay(5);
}
