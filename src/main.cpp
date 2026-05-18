#include <Arduino.h>
#include <cstdint>
#include <esp_bt.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_pm.h>
#include <esp_system.h>
#include <esp32-hal-cpu.h>

#include "log_tag.h"

#include "async/app_task.h"
#include "async/task_handles.h"

#include "config/app_config.h"
#include "hw/button.h"
#include "heart/counter.h"
#include "display/display.h"
#include "mqtt/mqtt.h"
#include "mqtt/config.h"
#include "network/network_task.h"
#include "ota/ota_task.h"
#include "hw/pins.h"
#include "web/admin.h"
#include "web/auth.h"
#include "wifi/wlan.h"

DEFINE_LOG_TAG("MAIN");

// After OTA: if image is pending verify, mark app valid (cancel rollback).
static void otaTryMarkFirmwareValidIfPendingVerify() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (running == nullptr) {
        return;
    }
    esp_ota_img_states_t imgState = ESP_OTA_IMG_UNDEFINED;
    if (esp_ota_get_state_partition(running, &imgState) != ESP_OK) {
        return;
    }
    if (imgState != ESP_OTA_IMG_PENDING_VERIFY) {
        return;
    }
    const esp_err_t v = esp_ota_mark_app_valid_cancel_rollback();
    if (v != ESP_OK) {
        ESP_LOGW(TAG, "esp_ota_mark_app_valid_cancel_rollback: %s", esp_err_to_name(v));
    } else {
        ESP_LOGI(TAG, "Firmware marked valid (rollback cancelled)");
    }
}

/** Same assignments as display/button (SPI + panel); do not use pins 6–11 (flash). */
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
    asyncInfraInit();
    setCpuFrequencyMhz(240);
    btStop();
    esp_bt_controller_mem_release(ESP_BT_MODE_BTDM);

    // DFS: idle min 80 MHz (WiFi). No light sleep (keep web + MQTT responsive).
    {
        esp_pm_config_t pm_cfg = {};
        pm_cfg.max_freq_mhz       = 240;
        pm_cfg.min_freq_mhz       = 80;
        pm_cfg.light_sleep_enable = false;
        const esp_err_t pm_err = esp_pm_configure(&pm_cfg);
        if (pm_err != ESP_OK && pm_err != ESP_ERR_NOT_SUPPORTED) {
            ESP_LOGW(TAG, "esp_pm_configure: %s", esp_err_to_name(pm_err));
        }
    }

    otaTryMarkFirmwareValidIfPendingVerify();

#if defined(CORE_DEBUG_LEVEL) && CORE_DEBUG_LEVEL > 0
    Serial.begin(115200);
#endif
    ESP_LOGI(TAG, "=== Chaya2MQTT === rst:%d", static_cast<int>(esp_reset_reason()));

    pinsInit();
    displayInit();
    displayStartTask();
    ESP_LOGI(TAG, "Display initialized");

    buttonInit();

    loadMQTTConfig();
    loadHeartCounter();
    configLoadResetPeriodFromNvs();
    configLoadWebAuthFromNvs();
    setupWiFi();

    mqttSetup();

    buttonSetAuthBlinkShortPressHandler(webAuthHandleButtonDuringAuthBlink);
    // Run startup blink before the button task touches the LED GPIO (otherwise both race on ledOutput).
    buttonStartupBlink();

    buttonStartTask();
    networkTaskStart();
    otaTaskStart();
    appTaskStart();

    ESP_LOGI(TAG, "Setup complete");

    {
        MqttConfig cfg{};
        mqttCfgSnapshot(&cfg);
        if (cfg.server[0] != '\0') {
            requestDeferredDrawHeartScreen();
        } else if (configIsApMode()) {
            requestDeferredDrawSplashScreen();
        }
    }

    buttonEnableLedGpioHoldForLightSleep();
}

void loop() {
    vTaskDelete(nullptr);
}
