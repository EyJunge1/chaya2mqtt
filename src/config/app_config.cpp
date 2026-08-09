#include "app_config.h"

#include "nvs_utils.h"
#include "nvs_keys.h"

#include <atomic>
#include <Arduino.h>
#include <esp_log.h>

#include "util/log_tag.h"

DEFINE_LOG_TAG("CFG");

// NVS namespace "cfg" — cached: display reset period.

static std::atomic<uint8_t> s_resetPeriodDaysCached{7};

void configLoadResetPeriodFromNvs() {
    const uint8_t raw = app_nvs::readUChar(kNvsNsCfg, kNvsKeyCfgRstPeriod, 7);
    if (raw == 0U) {
        s_resetPeriodDaysCached.store(0, std::memory_order_relaxed);
        return;
    }
    if (raw <= 30U) {
        s_resetPeriodDaysCached.store(raw, std::memory_order_relaxed);
        return;
    }
    s_resetPeriodDaysCached.store(7, std::memory_order_relaxed);
}

uint8_t configGetResetPeriodDays() {
    return s_resetPeriodDaysCached.load(std::memory_order_relaxed);
}

bool configSetResetPeriodDays(uint8_t days) {
    if (days > 30U) {
        days = 30U;
    }
    if (configGetResetPeriodDays() == days) {
        return true;
    }
    if (!app_nvs::writeUChar(kNvsNsCfg, kNvsKeyCfgRstPeriod, days)) {
        ESP_LOGE(TAG, "NVS cfg: failed to persist rstPeriod");
        return false;
    }
    s_resetPeriodDaysCached.store(days, std::memory_order_relaxed);
    return true;
}

void app_configResetRamAfterFactoryClear() {
    s_resetPeriodDaysCached.store(7, std::memory_order_relaxed);
}
