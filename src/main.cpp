#include <Arduino.h>
#include <esp_bt.h>
#include <esp_log.h>
#include <esp_pm.h>
#include <esp_system.h>
#include <esp32-hal-cpu.h>

#include "util/log_tag.h"

#include "async/app_task.h"
#include "async/task_handles.h"

#include "audio/audio.h"
#include "config/app_config.h"
#include "config/version.h"
#include "battery/battery.h"
#include "button/button.h"
#include "button/button_actions.h"
#include "hw/sd_hold.h"
#include "heart/counter.h"
#include "display/display.h"
#include "mqtt/mqtt.h"
#include "mqtt/config.h"
#include "network/network_task.h"
#include "ota/ota.h"
#include "ota/ota_task.h"
#include "web/admin.h"
#include "async/web_server_hooks.h"
#include "wifi/wlan.h"

DEFINE_LOG_TAG("MAIN");

namespace {
void onButtonRequestSend() {
    (void)chayaRequestSend();
}
bool onButtonSoftOffAllowed() {
    return !otaBlocksDestructiveAction();
}
void onButtonPerformSoftOff() {
    batteryPowerOffAndSleep();
}
} // namespace

void setup() {
    // Battery latch: must be HIGH before PWR is released or LiPo power cuts.
    pinMode(pins::kBatControl, OUTPUT);
    digitalWrite(pins::kBatControl, HIGH);

    // microSD: no driver / mount; hold CLK/DAT0/CMD LOW so the slot cannot float-draw.
    sdHoldOff();

    asyncInfraInit();
    setCpuFrequencyMhz(240);
    btStop();
    esp_bt_controller_mem_release(ESP_BT_MODE_BLE);

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
    batteryInit();
    audioInit();

#if defined(CORE_DEBUG_LEVEL) && CORE_DEBUG_LEVEL > 0
    Serial.begin(115200);
    // IDF component tags: closer to Espressif example verbosity for net/TLS debugging.
    esp_log_level_set("wifi", ESP_LOG_DEBUG);
    esp_log_level_set("mqtt_client", ESP_LOG_DEBUG);
    esp_log_level_set("esp-tls", ESP_LOG_INFO);
    esp_log_level_set("transport_base", ESP_LOG_INFO);
#endif
    ESP_LOGI(TAG, "=== Chaya2MQTT === rst:%d", static_cast<int>(esp_reset_reason()));
    ESP_LOGI(TAG, "Firmware %s | heap free=%zu min_free=%zu", APP_VERSION,
             static_cast<size_t>(esp_get_free_heap_size()),
             static_cast<size_t>(esp_get_minimum_free_heap_size()));

    loadMQTTConfig();
    loadHeartCounter();
    configLoadResetPeriodFromNvs();
    configLoadUiPrefsFromNvs();
    configLoadLedFromNvs();
    configLoadAudioFromNvs();
    configLoadDisplayViewFromNvs();
    // Waveshare 08_E_paper_test: paint the panel before bringing up Wi-Fi RF.
    WlanConfig bootWlan{};
    const bool haveSta = wlanLoadConfigFromNvs(&bootWlan) && bootWlan.ssid[0] != '\0';
    if (!haveSta) {
        if (wlanArmSetupApMode()) {
            displaySetContentAllowed(false);
            (void)displayRequest(DisplayMsg::Cmd::DrawSplash, DisplayRequestMode::BootIfChanged);
            if (!displayWaitDrawIdle(90000U)) {
                ESP_LOGW(TAG, "E-Ink splash wait timed out");
            }
        }
    }

    webAdminInstallServerHooks();
    webServerRegisterRoutes();
    buttonSetActionHooks(ButtonActionHooks{onButtonRequestSend, onButtonSoftOffAllowed, onButtonPerformSoftOff});
    setupWiFi();
    webServerBegin();

    mqttSetup();
    displaySetContentAllowed(!configIsApMode() && mqttCfgIsHeartReady());

    // Run startup blink before the button task touches the LED GPIO (otherwise both race on ledOutput).
    buttonStartupBlink();

    audioStartTask();
    buttonStartTask();
    networkTaskStart();
    otaTaskStart();
    appTaskStart();

    ESP_LOGI(TAG, "Setup complete");
}

void loop() {
    vTaskDelete(nullptr);
}
