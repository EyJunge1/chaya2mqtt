#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_err.h>
#include <esp_log.h>
#include <esp_task_wdt.h>

inline void chayaTaskWatchdogSubscribe(const char* logTag) {
    const TaskHandle_t h = xTaskGetCurrentTaskHandle();
    if (h == nullptr) {
        return;
    }
    esp_err_t err = esp_task_wdt_add(h);
    if (err == ESP_ERR_INVALID_STATE) {
        return;  // already in WDT
    }
    if (err != ESP_OK) {
        ESP_LOGW(logTag, "esp_task_wdt_add: %s", esp_err_to_name(err));
    }
}

inline void chayaTaskWatchdogReset() {
    static_cast<void>(esp_task_wdt_reset());
}
