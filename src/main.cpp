#include <Arduino.h>
#include <cstdint>
#include <esp_bt.h>
#include <esp_log.h>
#include <esp_pm.h>
#include <esp_system.h>
#include <esp32-hal-cpu.h>

#include "util/log_tag.h"

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
#include "web/admin.h"
#include "wifi/wlan.h"
#include "config/version.h"

DEFINE_LOG_TAG("MAIN");

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

    displayInit();
    displayStartTask();
    ESP_LOGI(TAG, "Display initialized");

    buttonInit();

#if defined(CORE_DEBUG_LEVEL) && CORE_DEBUG_LEVEL > 0
    Serial.begin(115200);
#endif
    ESP_LOGI(TAG, "=== Chaya2MQTT === rst:%d", static_cast<int>(esp_reset_reason()));
    ESP_LOGI(TAG, "Firmware %s | heap free=%zu min_free=%zu", APP_VERSION,
             static_cast<size_t>(esp_get_free_heap_size()),
             static_cast<size_t>(esp_get_minimum_free_heap_size()));

    loadMQTTConfig();
    loadHeartCounter();
    configLoadResetPeriodFromNvs();
    setupWiFi();

    mqttSetup();

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
