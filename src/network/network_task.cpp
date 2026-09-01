#include "network_task.h"

#include "async/event_types.h"
#include "async/task_config.h"
#include "async/task_handles.h"
#include "async/web_server_hooks.h"

#include "config/app_config.h"
#include "display/display.h"
#include "mqtt/config.h"
#include "mqtt/mqtt.h"
#include "wifi/wlan.h"

#include "diag/stack_monitor.h"
#include "diag/task_watchdog.h"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "util/log_tag.h"

DEFINE_LOG_TAG("NET");

// wlanLoop + mqttLoop (STA) + serialized admin net commands.

static bool s_mqttSettingsChangedDeferred = false;

static void mqttFinishSettingsApply() {
    mqttEndSettingsApply();
    mqttCfgSetApplyPending(false);
}

static void handleNetCommand(NetCmd cmd) {
    static const char *const kNetCmdNames[] = {
        "MqttSettingsChanged", "MqttKillClient",        "WifiGotIp", "WifiReconnect", "ChayaSendRequested",
        "ChayaPublish",        "FactoryResetRequested",
    };
    const unsigned idx = static_cast<unsigned>(cmd);
    ESP_LOGI(TAG, "netCmd=%s", idx < (sizeof(kNetCmdNames) / sizeof(kNetCmdNames[0])) ? kNetCmdNames[idx] : "?");
    switch (cmd) {
    case NetCmd::MqttSettingsChanged: {
        if (wlanEpdRefreshActive()) {
            ESP_LOGD(TAG, "MQTT settings apply deferred (EPD refresh active)");
            s_mqttSettingsChangedDeferred = true;
            break;
        }
        ESP_LOGI(TAG, "MQTT settings apply: start");
        mqttBeginSettingsApply();
        if (!mqttCfgHasUnappliedPending()) {
            mqttFinishSettingsApply();
            ESP_LOGI(TAG, "MQTT settings apply: nothing pending");
            break;
        }
        mqttDisconnect();
        chayaTaskWatchdogReset();
        mqttCfgApplyPendingToActive();
        if (mqttCfgMatchesNvs()) {
            mqttSetup();
            mqttPostponeConnect(3000UL);
            mqttFinishSettingsApply();
            chayaTaskWatchdogReset();
            // Waiting title ↔ operational heart (view change; bypass Content coalesce).
            displaySetContentAllowed(!configIsApMode() && mqttCfgIsHeartReady());
            if (mqttCfgIsHeartReady()) {
                (void)displayRequest(DisplayMsg::Cmd::DrawHeart, DisplayRequestMode::BootIfChanged);
            } else {
                (void)displayRequest(DisplayMsg::Cmd::DrawSplash, DisplayRequestMode::BootIfChanged);
            }
            ESP_LOGI(TAG, "MQTT settings apply: done (matches NVS, postpone=3000)");
            break;
        }
        if (!saveMQTTConfig()) {
            ESP_LOGW(TAG, "MQTT settings: NVS save failed — reloading from flash");
            loadMQTTConfig();
            mqttCfgSetNvsWriteFailed(true);
        } else {
            mqttCfgSetNvsWriteFailed(false);
        }
        chayaTaskWatchdogReset();
        mqttSetup();
        mqttPostponeConnect(3000UL);
        mqttFinishSettingsApply();
        chayaTaskWatchdogReset();
        // Waiting title ↔ operational heart (view change; bypass Content coalesce).
        displaySetContentAllowed(!configIsApMode() && mqttCfgIsHeartReady());
        if (mqttCfgIsHeartReady()) {
            (void)displayRequest(DisplayMsg::Cmd::DrawHeart, DisplayRequestMode::BootIfChanged);
        } else {
            (void)displayRequest(DisplayMsg::Cmd::DrawSplash, DisplayRequestMode::BootIfChanged);
        }
        ESP_LOGI(TAG, "MQTT settings apply: done (saved, postpone=3000)");
        break;
    }
    case NetCmd::MqttKillClient:
        if (wlanEpdRefreshActive()) {
            mqttRequestKillClientDeferred();
            break;
        }
        mqttDisconnect();
        break;
    case NetCmd::WifiGotIp:
        wlanHandleStaGotIpNetCmd();
        break;
    case NetCmd::WifiReconnect:
        wlanHandleStaReconnectNetCmd();
        break;
    case NetCmd::ChayaSendRequested:
        (void)chayaRequestSend();
        break;
    case NetCmd::ChayaPublish:
        mqttRunChayaPublishOnNetworkTask();
        break;
    case NetCmd::FactoryResetRequested:
        webServerEnd();
        resetAllSettings();
        break;
    }
}

static void networkTaskFn(void *) {
    chayaTaskWatchdogSubscribe(TAG);
    ESP_LOGI(TAG, "network task core=%d poll=%u/%u ms", static_cast<int>(xPortGetCoreID()),
             static_cast<unsigned>(kNetworkPollApMs), static_cast<unsigned>(kNetworkPollStaMs));
    static uint32_t s_stackLogCounter = 0;
    for (;;) {
        NetCmd cmd;
        const uint32_t pollMs = configIsApMode() ? kNetworkPollApMs : kNetworkPollStaMs;
        bool hasCmd = false;
        if (s_mqttSettingsChangedDeferred && !wlanEpdRefreshActive()) {
            s_mqttSettingsChangedDeferred = false;
            cmd = NetCmd::MqttSettingsChanged;
            hasCmd = true;
        } else {
            hasCmd = xQueueReceive(g_netCmdQueue, &cmd, pdMS_TO_TICKS(pollMs)) == pdTRUE;
        }

        if (hasCmd) {
            handleNetCommand(cmd);
        }

        wlanLoop();

        if (!configIsApMode()) {
            mqttLoop();
        }
        chayaTaskWatchdogReset();
        logTaskStackHighWaterPeriodic(TAG, s_stackLogCounter, 120);
    }
}

void networkTaskStart() {
    const BaseType_t ok = xTaskCreatePinnedToCore(networkTaskFn, "network", kNetworkTaskStackBytes, nullptr, 5, nullptr, 1);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "network task create failed");
        abort();
    }
}
