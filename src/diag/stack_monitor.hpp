#pragma once

#include <cstdint>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

/** Warn when free stack high-water drops below this (words → bytes via sizeof). */
inline constexpr UBaseType_t kStackHighWaterWarnBytes = 512U;

inline void logTaskStackHighWaterPeriodic(const char *tag, uint32_t &counter, uint32_t everyNLoops) {
    ++counter;
    if (everyNLoops == 0U || (counter % everyNLoops) != 0U) {
        return;
    }
    const UBaseType_t h = uxTaskGetStackHighWaterMark(nullptr);
#if defined(CORE_DEBUG_LEVEL) && CORE_DEBUG_LEVEL > 0
    ESP_LOGI(tag, "task stack high-water: %u bytes", static_cast<unsigned>(h));
#endif
    // Release builds: rate-limited WARN only when critically low (STAB-10).
    if (h < kStackHighWaterWarnBytes) {
        ESP_LOGW(tag, "task stack high-water low: %u bytes", static_cast<unsigned>(h));
    }
}
