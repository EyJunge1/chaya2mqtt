#include "ota_task.h"

#include "ota/ota.h"

#include "async/task_config.h"
#include "diag/stack_monitor.h"
#include "diag/task_watchdog.h"

#include <Arduino.h>
#include <cstdint>
#include <cstdlib>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "log_tag.h"

DEFINE_LOG_TAG("OTATASK");

// Isolated stack for GitHub probe + blocking OTA write.

static void otaTaskFn(void*) {
    static uint32_t s_stackLogCounter = 0;
    for (;;) {
        chayaTaskWatchdogUnsubscribe(TAG);
        otaLoop();
        chayaTaskWatchdogSubscribe(TAG);
        chayaTaskWatchdogReset();
        vTaskDelay(pdMS_TO_TICKS(100));
        logTaskStackHighWaterPeriodic(TAG, s_stackLogCounter, 600);
    }
}

void otaTaskStart() {
    const BaseType_t ok =
        xTaskCreatePinnedToCore(otaTaskFn, "ota", kOtaTaskStackBytes, nullptr, 4, nullptr, 1);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "OTA task create failed");
        abort();
    }
}
