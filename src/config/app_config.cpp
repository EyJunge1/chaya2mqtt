#include "app_config.h"

#include "nvs_utils.h"
#include "nvs_keys.h"
#include "constants.h"

#include <atomic>
#include <Arduino.h>
#include <cstring>
#include <esp_log.h>

#include "util/log_tag.h"

DEFINE_LOG_TAG("CFG");

// NVS namespace "cfg" — cached: display reset period, UI language, UI theme.

static std::atomic<uint8_t> s_resetPeriodDaysCached{7};
static char s_uiLangCached[3] = "en";
static char s_uiThemeCached[6] = "light";
static portMUX_TYPE s_uiPrefsMux = portMUX_INITIALIZER_UNLOCKED;

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

void configLoadUiPrefsFromNvs() {
    char lang[8] = {};
    char theme[8] = {};
    app_nvs::readString(kNvsNsCfg, kNvsKeyCfgUiLang, lang, sizeof(lang));
    app_nvs::readString(kNvsNsCfg, kNvsKeyCfgUiTheme, theme, sizeof(theme));

    portENTER_CRITICAL(&s_uiPrefsMux);
    if (uiLangSyntaxOk(lang)) {
        strlcpy(s_uiLangCached, lang, sizeof(s_uiLangCached));
    } else {
        strlcpy(s_uiLangCached, "en", sizeof(s_uiLangCached));
    }
    if (uiThemeSyntaxOk(theme)) {
        strlcpy(s_uiThemeCached, theme, sizeof(s_uiThemeCached));
    } else {
        strlcpy(s_uiThemeCached, "light", sizeof(s_uiThemeCached));
    }
    portEXIT_CRITICAL(&s_uiPrefsMux);
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

const char* configGetUiLang() {
    return s_uiLangCached;
}

bool configSetUiLang(const char* lang) {
    if (!uiLangSyntaxOk(lang)) {
        return false;
    }
    portENTER_CRITICAL(&s_uiPrefsMux);
    const bool same = strcmp(s_uiLangCached, lang) == 0;
    portEXIT_CRITICAL(&s_uiPrefsMux);
    if (same) {
        return true;
    }
    if (!app_nvs::writeString(kNvsNsCfg, kNvsKeyCfgUiLang, lang)) {
        ESP_LOGE(TAG, "NVS cfg: failed to persist ui_lang");
        return false;
    }
    portENTER_CRITICAL(&s_uiPrefsMux);
    strlcpy(s_uiLangCached, lang, sizeof(s_uiLangCached));
    portEXIT_CRITICAL(&s_uiPrefsMux);
    return true;
}

const char* configGetUiTheme() {
    return s_uiThemeCached;
}

bool configSetUiTheme(const char* theme) {
    if (!uiThemeSyntaxOk(theme)) {
        return false;
    }
    portENTER_CRITICAL(&s_uiPrefsMux);
    const bool same = strcmp(s_uiThemeCached, theme) == 0;
    portEXIT_CRITICAL(&s_uiPrefsMux);
    if (same) {
        return true;
    }
    if (!app_nvs::writeString(kNvsNsCfg, kNvsKeyCfgUiTheme, theme)) {
        ESP_LOGE(TAG, "NVS cfg: failed to persist ui_theme");
        return false;
    }
    portENTER_CRITICAL(&s_uiPrefsMux);
    strlcpy(s_uiThemeCached, theme, sizeof(s_uiThemeCached));
    portEXIT_CRITICAL(&s_uiPrefsMux);
    return true;
}

void app_configResetRamAfterFactoryClear() {
    s_resetPeriodDaysCached.store(7, std::memory_order_relaxed);
    portENTER_CRITICAL(&s_uiPrefsMux);
    strlcpy(s_uiLangCached, "en", sizeof(s_uiLangCached));
    strlcpy(s_uiThemeCached, "light", sizeof(s_uiThemeCached));
    portEXIT_CRITICAL(&s_uiPrefsMux);
}
