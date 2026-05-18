#include "network_task.h"

#include "async/event_types.h"
#include "async/task_handles.h"
#include "web/admin_globals.h"

#include "config/app_config.h"
#include "display/display.h"
#include "heart/counter.h"
#include "mqtt/config.h"
#include "mqtt/mqtt.h"
#include "ota/ota.h"
#include "wifi/wlan.h"

#include "diag/stack_monitor.h"
#include "diag/task_watchdog.h"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "log_tag.h"

DEFINE_LOG_TAG("NET");

// wlanLoop + mqttLoop (STA) + serialized admin net commands.

static void handleNetCommand(NetCmd cmd) {
    switch (cmd) {
    case NetCmd::MqttSettingsChanged:
        mqttCfgApplyPendingToActive();
        if (!saveMQTTConfig()) {
            ESP_LOGW(TAG, "MQTT settings: NVS save failed — reloading from flash");
            loadMQTTConfig();
            g_webAdminMqttNvsWriteFailed.store(true, std::memory_order_release);
        } else {
            g_webAdminMqttNvsWriteFailed.store(false, std::memory_order_release);
        }
        mqttDisconnect();
        mqttSetup();
        mqttPostponeConnect(3000UL);
        requestHeartRedraw();
        break;
    case NetCmd::MqttReconnect:
        mqttDisconnect();
        mqttSetup();
        break;
    case NetCmd::OtaCheckRequested:
        otaQueueGithubCheck();
        break;
    }
}

static void networkTaskFn(void*) {
    chayaTaskWatchdogSubscribe(TAG);
    static uint32_t s_stackLogCounter = 0;
    for (;;) {
        NetCmd cmd;
        const bool hasCmd = xQueueReceive(g_netCmdQueue, &cmd, pdMS_TO_TICKS(500)) == pdTRUE;

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
    const BaseType_t ok =
        xTaskCreatePinnedToCore(networkTaskFn, "network", 7168, nullptr, 5, nullptr, 1);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "network task create failed");
        abort();
    }
}
