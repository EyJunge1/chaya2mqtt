#include <Arduino.h>
#include <algorithm>
#include <cstdint>
#include <driver/gpio.h>
#include <esp_bt.h>
#include <esp_log.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <esp32-hal-cpu.h>

#include "button.h"
#include "config.h"
#include "display.h"
#include "mqtt.h"

#if defined(CORE_DEBUG_LEVEL) && CORE_DEBUG_LEVEL > 0
static const char* TAG = "MAIN";
#else
static constexpr const char* TAG __attribute__((unused)) = "";
#endif

/** Light-Sleep: kurz bei aktiver LED-Sequenz, laenger im Idle (Taster per GPIO-, WiFi per Event-Wakeup). */
static constexpr uint64_t kLightSleepActiveUs = 10000ULL;   // 10 ms
/** Idle: 2 s -- haeufigeres Aufwachen, damit PubSubClient::loop()/Keepalive beim Broker zuverlaessiger laufen. */
static constexpr uint64_t kLightSleepIdleUs = 2000000ULL;  // 2 s

static uint64_t computeLightSleepTimerUs() {
    if (configIsSetupPortalActive()) {
        return kLightSleepActiveUs;
    }
    if (buttonIsLedTxSequenceActive()) {
        return kLightSleepActiveUs;
    }
    const unsigned long mqttWaitMs = mqttMillisUntilNextConnectAttempt();
    if (mqttWaitMs > 0) {
        uint64_t alignUs = static_cast<uint64_t>(mqttWaitMs) * 1000ULL;
        constexpr uint64_t kMinAlignUs = 10000ULL;
        alignUs = std::max(kMinAlignUs, std::min(alignUs, kLightSleepIdleUs));
        return alignUs;
    }
    return kLightSleepIdleUs;
}

/** Belegungen wie display.cpp / button.cpp; Pins 6–11 (Flash) nicht anfassen. */
static void pinsInit() {
    pinMode(25, INPUT);
    pinMode(26, OUTPUT);
    pinMode(27, OUTPUT);
    pinMode(12, INPUT);
    pinMode(13, OUTPUT);
    pinMode(14, OUTPUT);
    pinMode(15, OUTPUT);
    digitalWrite(15, HIGH);
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
    ESP_LOGI(TAG, "=== chaya2mqtt === rst:%d", static_cast<int>(esp_reset_reason()));

    pinsInit();
    displayInit();
    ESP_LOGI(TAG, "Display initialisiert");

    buttonInit();

    loadMQTTConfig();
    loadHeartCounter();
    setupWiFi();

    mqttSetup();

    armLightSleepStaticWakeups();

    ESP_LOGI(TAG, "Setup abgeschlossen");

    if (mqttCfg.server[0] != '\0') {
        drawHeartWithNumber();
    } else {
        drawSplashScreen();
    }

    buttonStartupBlink();
    buttonEnableLedGpioHoldForLightSleep();
}

void loop() {
    const unsigned long now = millis();

    buttonLoop();
    buttonAdvanceLedSequence();
    configLoop();
    mqttLoop();
    maybeSaveHeartCounter();

    /* WiFi-Reconnect: WiFi.onEvent in setupWiFi (siehe configLoop / webAdminLoop). */

    if (consumeHeartRedraw()) {
        drawHeartWithNumber();
    }

#if defined(CORE_DEBUG_LEVEL) && CORE_DEBUG_LEVEL > 0
    static unsigned long lastDbg = 0;
    if (now - lastDbg > 5000) {
        buttonDebugStatus();
        lastDbg = now;
    }
#endif

    if (configIsSetupPortalActive()) {
        delay(10); /* FreeRTOS-Tasks (WiFi/DNS/HTTP) laufen lassen; etwas weniger Last im AP */
    } else if (mqttIsConnected()) {
        /* Kein Light Sleep bei aktiver TLS-Session: sonst BEACON_TIMEOUT / Socket-Fehler. */
        delay(50);
    } else if (mqttCfg.server[0] == '\0') {
        /* MQTT noch nicht konfiguriert: Web-Admin muss erreichbar sein (mDNS). */
        delay(50);
    } else {
        static uint64_t lastArmedLightSleepTimerUs = UINT64_MAX;
        const uint64_t lightSleepTimerUs = computeLightSleepTimerUs();
        if (lightSleepTimerUs != lastArmedLightSleepTimerUs) {
            armLightSleepTimerWakeup(lightSleepTimerUs);
            lastArmedLightSleepTimerUs = lightSleepTimerUs;
        }
        esp_light_sleep_start();
    }
}
