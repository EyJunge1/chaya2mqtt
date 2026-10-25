#include "ota_task.hpp"

#include "ota/ota.hpp"

#include "async/task_config.hpp"
#include "diag/stack_monitor.hpp"
#include "diag/task_watchdog.hpp"

#include <Arduino.h>
#include <cstdint>
#include <cstdlib>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "util/log_tag.hpp"

DEFINE_LOG_TAG("OTATASK");

// Isolated stack for GitHub probe + blocking OTA write.

static TaskHandle_t s_otaTaskHandle = nullptr;

constexpr uint32_t kOtaIdleWaitMs = 60000U;
constexpr uint32_t kOtaIdlePollMs = 1000U;

static void waitForOtaWakeOrTimeout() {
    uint32_t remainingMs = kOtaIdleWaitMs;
    while (remainingMs > 0) {
        const uint32_t sliceMs = (remainingMs > kOtaIdlePollMs) ? kOtaIdlePollMs : remainingMs;
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(sliceMs)) > 0) {
            return;
        }
        chayaTaskWatchdogReset();
        remainingMs -= sliceMs;
    }
}

static void otaTaskFn(void *) {
    static uint32_t s_stackLogCounter = 0;
    for (;;) {
        chayaTaskWatchdogUnsubscribe(TAG);
        otaLoop();
        chayaTaskWatchdogSubscribe(TAG);
        chayaTaskWatchdogReset();
        logTaskStackHighWaterPeriodic(TAG, s_stackLogCounter, 600);
        waitForOtaWakeOrTimeout();
    }
}

void otaTaskWake() {
    if (s_otaTaskHandle != nullptr) {
        (void)xTaskNotifyGive(s_otaTaskHandle);
    }
}

void otaTaskStart() {
    const BaseType_t ok = xTaskCreatePinnedToCore(otaTaskFn, "ota", kOtaTaskStackBytes, nullptr, 4, &s_otaTaskHandle, 1);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "OTA task create failed");
        abort();
    }
}
