#include "network_task.h"

#include "async/event_types.h"
#include "async/task_config.h"
#include "async/task_handles.h"
#include "web/admin_globals.h"

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

static void handleNetCommand(NetCmd cmd) {
    switch (cmd) {
    case NetCmd::MqttSettingsChanged: {
        if (wlanEpdRefreshActive()) {
            s_mqttSettingsChangedDeferred = true;
            break;
        }
        mqttBeginSettingsApply();
        if (!mqttCfgHasUnappliedPending()) {
            mqttEndSettingsApply();
            break;
        }
        mqttDisconnect();
        chayaTaskWatchdogReset();
        mqttCfgApplyPendingToActive();
        if (mqttCfgMatchesNvs()) {
            mqttSetup();
            mqttPostponeConnect(3000UL);
            mqttEndSettingsApply();
            chayaTaskWatchdogReset();
            break;
        }
        if (!saveMQTTConfig()) {
            ESP_LOGW(TAG, "MQTT settings: NVS save failed — reloading from flash");
            loadMQTTConfig();
            g_webAdminMqttNvsWriteFailed.store(true, std::memory_order_release);
        } else {
            g_webAdminMqttNvsWriteFailed.store(false, std::memory_order_release);
        }
        chayaTaskWatchdogReset();
        mqttSetup();
        mqttPostponeConnect(3000UL);
        mqttEndSettingsApply();
        chayaTaskWatchdogReset();
        requestHeartRedraw();
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
        (void)mqttPublishChayaAndApplySentCounters();
        break;
    case NetCmd::FactoryResetRequested:
        resetAllSettings();
        break;
    }
}

static void networkTaskFn(void*) {
    chayaTaskWatchdogSubscribe(TAG);
    ESP_LOGI(TAG, "network task core=%d poll=%u/%u ms", static_cast<int>(xPortGetCoreID()),
             static_cast<unsigned>(kNetworkPollApMs),
             static_cast<unsigned>(kNetworkPollStaMs));
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
    const BaseType_t ok = xTaskCreatePinnedToCore(networkTaskFn, "network",
                                                  kNetworkTaskStackBytes, nullptr, 5, nullptr, 1);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "network task create failed");
        abort();
    }
}
