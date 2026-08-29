#pragma once

#include <cstdint>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

inline void logTaskStackHighWaterPeriodic(const char* tag, uint32_t& counter, uint32_t everyNLoops) {
    ++counter;
    if (everyNLoops == 0U || (counter % everyNLoops) != 0U) {
        return;
    }
#if defined(CORE_DEBUG_LEVEL) && CORE_DEBUG_LEVEL > 0
    const UBaseType_t h = uxTaskGetStackHighWaterMark(nullptr);
    ESP_LOGI(tag, "task stack high-water: %u bytes", static_cast<unsigned>(h));
#else
    static_cast<void>(tag);
#endif
}
