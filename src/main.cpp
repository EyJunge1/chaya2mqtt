#include <Arduino.h>
#include <WiFi.h>
#include <driver/gpio.h>
#include <esp_sleep.h>

#include "button.h"
#include "config.h"
#include "display.h"
#include "mqtt.h"

static constexpr unsigned long kWifiReconnectIntervalMs = 30000;
static constexpr unsigned long kWifiHardReconnectGapMs = 100;

/** Light-Sleep: kurz bei aktiver LED-Sequenz, laenger im Idle (Taster weiter per GPIO-Wakeup). */
static constexpr uint64_t kLightSleepActiveUs = 10000ULL;   // 10 ms
static constexpr uint64_t kLightSleepIdleUs = 150000ULL;    // 150 ms

static uint64_t computeLightSleepTimerUs() {
    if (buttonIsLedTxSequenceActive()) {
        return kLightSleepActiveUs;
    }
    return kLightSleepIdleUs;
}

static void armLightSleepWakeupSources(uint64_t timerUs) {
    esp_sleep_enable_timer_wakeup(timerUs);
    gpio_wakeup_enable(static_cast<gpio_num_t>(kButtonGpio), GPIO_INTR_HIGH_LEVEL);
    esp_sleep_enable_gpio_wakeup();
}

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

    armLightSleepWakeupSources(computeLightSleepTimerUs());

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
    static int wifiReconnectAttempts = 0;
    static bool wifiHardReconnectPending = false;
    static unsigned long wifiHardReconnectSinceMs = 0;

    if (WiFi.status() != WL_CONNECTED) { // NOLINT(readability-static-accessed-through-instance)
        if (wifiWasConnected) {
            wifiReconnectDueImmediately = true;
            wifiWasConnected = false;
        }
        if (wifiHardReconnectPending) {
            if (now - wifiHardReconnectSinceMs >= kWifiHardReconnectGapMs) {
                WiFi.begin();
                wifiHardReconnectPending = false;
                wifiReconnectAttempts = 0;
            }
        } else if (wifiReconnectDueImmediately || now - lastWifiReconnectMs >= kWifiReconnectIntervalMs) {
            wifiReconnectDueImmediately = false;
            lastWifiReconnectMs = now;
            Serial.println("WiFi verloren! Versuche Reconnect...");
            if (wifiReconnectAttempts >= 3) {
                WiFi.disconnect(false);
                wifiHardReconnectPending = true;
                wifiHardReconnectSinceMs = now;
            } else {
                WiFi.reconnect();
                wifiReconnectAttempts++;
            }
        }
    } else {
        wifiHardReconnectPending = false;
        wifiReconnectAttempts = 0;
        if (!wifiWasConnected) {
            lastWifiReconnectMs = now;
        }
        wifiWasConnected = true;
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

    armLightSleepWakeupSources(computeLightSleepTimerUs());
    esp_light_sleep_start();
}
