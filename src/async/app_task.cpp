#include "app_task.h"

#include "async/task_handles.h"
#include "config/app_config.h"
#include "heart/counter.h"
#include "display/display.h"
#include "web/admin.h"
#include "ota/ota.h"
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

#include "log_tag.h"

DEFINE_LOG_TAG("APP");

// Housekeeping: deferred web work, counter baselines/NVS, not time-critical.

static TaskHandle_t s_appTaskHandle = nullptr;

void appTaskNotify() {
    if (s_appTaskHandle != nullptr) {
        xTaskNotifyGive(s_appTaskHandle);
    }
}

static void appTaskFn(void*) {
    chayaTaskWatchdogSubscribe(TAG);
    static uint32_t s_stackLogCounter = 0;
    static uint8_t  s_healthLoopsRemaining = 3;
    static uint32_t s_heapLogCounter       = 0;
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500));

        if (s_healthLoopsRemaining > 0U && wlanIsSetupComplete() && wlanIsBootWifiSettled()) {
            --s_healthLoopsRemaining;
            if (s_healthLoopsRemaining == 0U) {
                otaTryMarkValidAfterHealthCheck();
            }
        }

        webAdminLoop();

        if (!configIsApMode()) {
            maybePeriodicallyResetCounters();
            maybeResetDisplayBaselinesWhenCapped();
        }
        maybeSaveHeartCounter();
        maybeSaveHeartSentCounter();

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
