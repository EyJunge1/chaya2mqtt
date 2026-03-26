#include <Arduino.h>
#include <WiFi.h>
#include <algorithm>
#include <cstdint>
#include <driver/gpio.h>
#include <esp_bt.h>
#include <esp_sleep.h>
#include <esp32-hal-cpu.h>

#include "button.h"
#include "config.h"
#include "display.h"
#include "mqtt.h"

#if defined(CORE_DEBUG_LEVEL) && CORE_DEBUG_LEVEL > 0
#define MAIN_DBG_PRINT(x) Serial.print(x)
#define MAIN_DBG_PRINTLN(x) Serial.println(x)
#else
#define MAIN_DBG_PRINT(x) ((void)0)
#define MAIN_DBG_PRINTLN(x) ((void)0)
#endif

static constexpr unsigned long kWifiReconnectIntervalMs = 30000;
static constexpr unsigned long kWifiReconnectBackoffMaxMs = 300000;
static constexpr unsigned long kWifiHardReconnectGapMs = 100;

/** Light-Sleep: kurz bei aktiver LED-Sequenz, laenger im Idle (Taster per GPIO-, WiFi per Event-Wakeup). */
static constexpr uint64_t kLightSleepActiveUs = 10000ULL;   // 10 ms
static constexpr uint64_t kLightSleepIdleUs = 2000000ULL;  // 2 s (WiFi-Wakeup weckt bei Bedarf frueher)
static constexpr uint64_t kLightSleepWifiHardReconnectGapUs =
    static_cast<uint64_t>(kWifiHardReconnectGapMs) * 1000ULL;

static unsigned long lastWifiReconnectMs = 0;
static bool wifiWasConnected = true;
static bool wifiReconnectDueImmediately = false;
static int wifiReconnectAttempts = 0;
static bool wifiHardReconnectPending = false;
static unsigned long wifiHardReconnectSinceMs = 0;
static unsigned long wifiReconnectBackoffMs = kWifiReconnectIntervalMs;

static uint64_t computeLightSleepTimerUs() {
    if (buttonIsLedTxSequenceActive()) {
        return kLightSleepActiveUs;
    }
    if (wifiHardReconnectPending) {
        return kLightSleepWifiHardReconnectGapUs;
    }
    return kLightSleepIdleUs;
}

/** Einmalig in setup(): GPIO- und WiFi-Wakeup aendern sich nicht. */
static void armLightSleepStaticWakeups() {
    gpio_wakeup_enable(static_cast<gpio_num_t>(kButtonGpio), GPIO_INTR_HIGH_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    esp_sleep_enable_wifi_wakeup();
}

static void armLightSleepTimerWakeup(uint64_t timerUs) {
    esp_sleep_enable_timer_wakeup(timerUs);
}

void setup() {
    setCpuFrequencyMhz(80);
    btStop();
    esp_bt_controller_mem_release(ESP_BT_MODE_BTDM);

#if defined(CORE_DEBUG_LEVEL) && CORE_DEBUG_LEVEL > 0
    Serial.begin(115200);
#endif
    MAIN_DBG_PRINTLN("=== ESP32 Rotes Herz Display mit MQTT ===");

    displayInit();
    MAIN_DBG_PRINTLN("Display initialisiert...");

    buttonInit();

    loadMQTTConfig();
    loadHeartCounter();
    setupWiFi();

    mqttSetup();

    armLightSleepStaticWakeups();
    armLightSleepTimerWakeup(computeLightSleepTimerUs());

    MAIN_DBG_PRINTLN("Setup abgeschlossen");

    drawHeartWithNumber();

    buttonStartupBlink();
}

void loop() {
    const unsigned long now = millis();

    buttonLoop();
    checkLEDStatus();
    mqttLoop();
    maybeSaveHeartCounter();

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
        } else if (wifiReconnectDueImmediately || now - lastWifiReconnectMs >= wifiReconnectBackoffMs) {
            const bool triggeredByImmediate = wifiReconnectDueImmediately;
            wifiReconnectDueImmediately = false;
            lastWifiReconnectMs = now;
            if (!triggeredByImmediate) {
                wifiReconnectBackoffMs =
                    std::min(wifiReconnectBackoffMs * 2UL, kWifiReconnectBackoffMaxMs);
            }
            MAIN_DBG_PRINTLN("WiFi verloren! Versuche Reconnect...");
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
        wifiReconnectBackoffMs = kWifiReconnectIntervalMs;
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

    static uint64_t lastArmedLightSleepTimerUs = UINT64_MAX;
    const uint64_t lightSleepTimerUs = computeLightSleepTimerUs();
    if (lightSleepTimerUs != lastArmedLightSleepTimerUs) {
        armLightSleepTimerWakeup(lightSleepTimerUs);
        lastArmedLightSleepTimerUs = lightSleepTimerUs;
    }
    esp_light_sleep_start();
}
