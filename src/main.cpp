#include <Arduino.h>
#include <cstdint>
#include <esp_bt.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp32-hal-cpu.h>

#include "button.h"
#include "counter.h"
#include "display.h"
#include "mqtt.h"
#include "mqtt_config.h"
#include "pins.h"
#include "web/admin.h"
#include "web/auth.h"
#include "wlan.h"

#if defined(CORE_DEBUG_LEVEL) && CORE_DEBUG_LEVEL > 0
static const char* TAG = "MAIN";
#else
static constexpr const char* TAG __attribute__((unused)) = "";
#endif

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
    configLoadWebAuthFromNvs();
    setupWiFi();

    mqttSetup();

    buttonSetAuthBlinkShortPressHandler(webAuthHandleButtonDuringAuthBlink);

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
        maybeResetDisplayBaselinesWhenCapped();
    }
    mqttLoop();
    maybeSaveHeartCounter();
    maybeSaveHeartSentCounter();

    /* WiFi-Reconnect: WiFi.onEvent in setupWiFi (see wlanLoop / webAdminLoop). */

    displayProcessDeferredDrawsOnMainTask();
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

    /*
     * Do not use esp_light_sleep_start(): with STA + MQTT backoff it correlated with rst:7 WDT resets
     * and AUTH_FAIL cascades on reconnect. USB-powered device — uniform delay is stable enough.
     */
    if (configIsApMode()) {
        delay(10); /* FreeRTOS (WiFi/DNS/HTTP) */
    } else {
        delay(50);
    }
}
