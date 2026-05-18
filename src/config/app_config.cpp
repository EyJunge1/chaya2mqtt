#include "app_config.h"

#include "nvs_utils.h"

#include <atomic>
#include <Arduino.h>
#include <esp_log.h>

#include "log_tag.h"

DEFINE_LOG_TAG("CFG");

// NVS namespace "cfg" — cached: display reset period, web auth enable.

static constexpr const char kNvNamespaceCfg[] = "cfg";
static constexpr const char kNvRstPeriod[]    = "rstPeriod";
static constexpr const char kNvAuthEn[]       = "authEn";

static std::atomic<uint8_t> s_resetPeriodDaysCached{7};
static std::atomic<bool>    s_webAuthEnabledCached{false};

void configLoadWebAuthFromNvs() {
    s_webAuthEnabledCached.store((app_nvs::readUChar(kNvNamespaceCfg, kNvAuthEn, 0) != 0),
                                 std::memory_order_relaxed);
}

bool configGetWebAuthEnabled() {
    return s_webAuthEnabledCached.load(std::memory_order_relaxed);
}

bool configSetWebAuthEnabled(bool enabled) {
    if (!app_nvs::writeUChar(kNvNamespaceCfg, kNvAuthEn, enabled ? 1 : 0)) {
        ESP_LOGE(TAG, "NVS cfg: failed to persist authEn");
        return false;
    }
    s_webAuthEnabledCached.store(enabled, std::memory_order_relaxed);
    return true;
}

void configLoadResetPeriodFromNvs() {
    const uint8_t raw = app_nvs::readUChar(kNvNamespaceCfg, kNvRstPeriod, 7);
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
    if (!app_nvs::writeUChar(kNvNamespaceCfg, kNvRstPeriod, days)) {
        ESP_LOGE(TAG, "NVS cfg: failed to persist rstPeriod");
        return false;
    }
    s_resetPeriodDaysCached.store(days, std::memory_order_relaxed);
    return true;
}

void app_configResetRamAfterFactoryClear() {
    s_webAuthEnabledCached.store(false, std::memory_order_relaxed);
    s_resetPeriodDaysCached.store(7, std::memory_order_relaxed);
}
