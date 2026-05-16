#include "app_config.h"

#include "nvs_utils.h"

#include <Arduino.h>
#include <esp_log.h>

#if defined(CORE_DEBUG_LEVEL) && CORE_DEBUG_LEVEL > 0
static const char* TAG = "CFG";
#else
static constexpr const char* TAG __attribute__((unused)) = "";
#endif

static constexpr const char kNvNamespaceCfg[] = "cfg";
static constexpr const char kNvRstPeriod[]    = "rstPeriod";
static constexpr const char kNvAuthEn[]       = "authEn";

static uint8_t s_resetPeriodDaysCached = 7;
static bool    s_webAuthEnabledCached  = false;

void configLoadWebAuthFromNvs() {
    s_webAuthEnabledCached = (app_nvs::readUChar(kNvNamespaceCfg, kNvAuthEn, 0) != 0);
}

bool configGetWebAuthEnabled() {
    return s_webAuthEnabledCached;
}

void configSetWebAuthEnabled(bool enabled) {
    if (!app_nvs::writeUChar(kNvNamespaceCfg, kNvAuthEn, enabled ? 1 : 0)) {
        ESP_LOGE(TAG, "NVS cfg: authEn schreiben fehlgeschlagen");
        return;
    }
    s_webAuthEnabledCached = enabled;
}

void configLoadResetPeriodFromNvs() {
    const uint8_t raw = app_nvs::readUChar(kNvNamespaceCfg, kNvRstPeriod, 7);
    if (raw == 0U) {
        s_resetPeriodDaysCached = 0;
        return;
    }
    if (raw <= 30U) {
        s_resetPeriodDaysCached = raw;
        return;
    }
    s_resetPeriodDaysCached = 7;
}

uint8_t configGetResetPeriodDays() {
    return s_resetPeriodDaysCached;
}

void configSetResetPeriodDays(uint8_t days) {
    if (days > 30U) {
        days = 30U;
    }
    if (!app_nvs::writeUChar(kNvNamespaceCfg, kNvRstPeriod, days)) {
        ESP_LOGE(TAG, "NVS cfg: rstPeriod schreiben fehlgeschlagen");
        return;
    }
    s_resetPeriodDaysCached = days;
}

void app_configResetRamAfterFactoryClear() {
    s_webAuthEnabledCached  = false;
    s_resetPeriodDaysCached = 7;
}
