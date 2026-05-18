#include "app_task.h"

#include "async/task_handles.h"
#include "config/app_config.h"
#include "heart/counter.h"
#include "display/display.h"
#include "web/admin.h"
#include "wifi/wlan.h"

#include "diag/stack_monitor.h"
#include "diag/task_watchdog.h"

#include <Arduino.h>
#include <cstdint>
#include <cstdlib>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "log_tag.h"

DEFINE_LOG_TAG("APP");

// Housekeeping: deferred web work, counter baselines/NVS, not time-critical.

static void appTaskFn(void*) {
    chayaTaskWatchdogSubscribe(TAG);
    static uint32_t s_stackLogCounter = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(500));

        webAdminLoop();

        if (!configIsApMode()) {
            maybePeriodicallyResetCounters();
            maybeResetDisplayBaselinesWhenCapped();
        }
        maybeSaveHeartCounter();
        maybeSaveHeartSentCounter();

        chayaTaskWatchdogReset();
        logTaskStackHighWaterPeriodic(TAG, s_stackLogCounter, 120);
    }
}

void appTaskStart() {
    const BaseType_t ok = xTaskCreatePinnedToCore(appTaskFn, "app", 4096, nullptr, 4, nullptr, 1);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "app task create failed");
        abort();
    }
}
