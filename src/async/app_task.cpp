#include "app_task.h"

#include "async/task_handles.h"
#include "config/app_config.h"
#include "heart/counter.h"
#include "display/display.h"
#include "display/display_config.h"
#include "display/display_link_pure.h"
#include "hw/battery.h"
#include "hw/battery_config.h"
#include "mqtt/mqtt.h"
#include "web/admin.h"
#include "ota/ota.h"
#include "ota/ota_health.h"
#include "wifi/wlan.h"

#include "async/task_config.h"
#include "diag/stack_monitor.h"
#include "diag/task_watchdog.h"

#include <Arduino.h>
#include <cstdint>
#include <cstdlib>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "util/log_tag.h"

DEFINE_LOG_TAG("APP");

// Housekeeping: deferred web work, counter baselines/NVS, not time-critical.

static TaskHandle_t s_appTaskHandle = nullptr;

void appTaskNotify() {
    if (s_appTaskHandle != nullptr) {
        xTaskNotifyGive(s_appTaskHandle);
    }
}

static void appTaskPollDisplayLinkStatus() {
    if (configIsApMode() || !wlanIsSetupComplete()) {
        return;
    }

    static DisplayLinkState s_link{};
    static DisplayHeartIcon s_lastIcon = DisplayHeartIcon::Filled;

    const bool wifiConnected = wlanStaConnectedOk();
    const bool mqttConnected = mqttIsConnected();
    const DisplayHeartIcon icon = displayHeartIconDecide(
        false, wifiConnected, mqttConnected, millis(), kDisplayOfflineGraceMs, s_link);
    if (icon == s_lastIcon) {
        return;
    }
    s_lastIcon = icon;
    displaySetDesiredHeartIcon(icon);
    requestHeartRedraw();
    ESP_LOGI(TAG, "display heart icon -> %s",
             icon == DisplayHeartIcon::Crack ? "crack" : "filled");
}

static void appTaskFn(void*) {
    chayaTaskWatchdogSubscribe(TAG);
    static uint32_t s_stackLogCounter = 0;
    static uint32_t s_heapLogCounter  = 0;
    static uint32_t s_batterySkip     = 60U;
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500));

        if (otaHealthWindowElapsed(wlanIsSetupComplete(), wlanIsBootWifiSettled(),
                                   wlanBootSettledAtMs(), millis())) {
            otaTryMarkValidAfterHealthCheck();
        }

        webAdminLoop();

        if (!configIsApMode()) {
            maybePeriodicallyResetCounters();
            maybeResetDisplayBaselinesWhenCapped();
        }
        maybeSaveHeartCounter();
        maybeSaveHeartSentCounter();

        appTaskPollDisplayLinkStatus();

        ++s_batterySkip;
        if (s_batterySkip >= (kBatteryPollMs / 500UL)) {
            s_batterySkip = 0U;
            batteryPoll();
            // Heart redraw decide skips when battery icon level is unchanged.
            requestHeartRedraw();
        }

        ++s_heapLogCounter;
        if (s_heapLogCounter >= 120U) {
            s_heapLogCounter = 0U;
            ESP_LOGI(TAG, "heap free=%zu min=%zu largest=%zu",
                     static_cast<size_t>(esp_get_free_heap_size()),
                     static_cast<size_t>(esp_get_minimum_free_heap_size()),
                     static_cast<size_t>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
        }

        chayaTaskWatchdogReset();
        logTaskStackHighWaterPeriodic(TAG, s_stackLogCounter, 120);
    }
}

void appTaskStart() {
    const BaseType_t ok = xTaskCreatePinnedToCore(appTaskFn, "app", kAppTaskStackBytes, nullptr, 4,
                                                  &s_appTaskHandle, 1);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "app task create failed");
        abort();
    }
}
