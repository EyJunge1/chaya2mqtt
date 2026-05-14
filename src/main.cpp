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
#include "counter.h"
#include "display.h"
#include "mqtt.h"
#include "mqtt_config.h"
#include "pins.h"
#include "web_admin.h"
#include "wlan.h"

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
    if (configIsApMode()) {
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

/** Same assignments as display/button (GxEPD2 SPI + panel); do not use pins 6–11 (flash). */
static void pinsInit() {
    pinMode(pins::kDisplayBusy, INPUT);
    pinMode(pins::kDisplayRst, OUTPUT);
    pinMode(pins::kDisplayDc, OUTPUT);
    pinMode(pins::kSpiMiso, INPUT);
    pinMode(pins::kSpiSck, OUTPUT);
    pinMode(pins::kSpiMosi, OUTPUT);
    pinMode(pins::kSpiCs, OUTPUT);
    digitalWrite(pins::kSpiCs, HIGH);
}

/** Einmalig in setup(): GPIO- und WiFi-Wakeup aendern sich nicht. */
static void armLightSleepStaticWakeups() {
    gpio_wakeup_enable(static_cast<gpio_num_t>(pins::kButton), GPIO_INTR_HIGH_LEVEL);
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
    ESP_LOGI(TAG, "=== Chaya2MQTT === rst:%d", static_cast<int>(esp_reset_reason()));

    pinsInit();
    displayInit();
    ESP_LOGI(TAG, "Display initialisiert");

    buttonInit();

    loadMQTTConfig();
    loadHeartCounter();
    configLoadResetPeriodFromNvs();
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
    wlanLoop();
    webAdminLoop();
    if (!configIsApMode()) {
        maybePeriodicallyResetCounters();
    }
    mqttLoop();
    maybeSaveHeartCounter();
    maybeSaveHeartSentCounter();

    /* WiFi-Reconnect: WiFi.onEvent in setupWiFi (see wlanLoop / webAdminLoop). */

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

    if (configIsApMode()) {
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
