#include "counter.h"

#include "constants.h"
#include "display.h"
#include "wlan.h"

#include <Arduino.h>
#include <Preferences.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <esp_log.h>
#include <time.h>

#if defined(CORE_DEBUG_LEVEL) && CORE_DEBUG_LEVEL > 0
static const char* TAG = "CTR";
#else
static constexpr const char* TAG __attribute__((unused)) = "";
#endif

int heartCounter     = 0;
int heartSentCounter = 0;

int counterBaseline   = 0;
int sentCountBaseline = 0;

static uint32_t s_lastResetCalendarDayUtc = UINT32_MAX;

static constexpr const char kNvRstPeriod[] = "rstPeriod";

static constexpr const char kNvAuthEn[] = "authEn";

static bool s_resetPeriodWeeklyCached = false;
static bool s_webAuthEnabledCached     = false;

static int           lastCommittedHeartCounter                = 0;
static unsigned long lastHeartCounterSaveMs                   = 0;
static int           lastCommittedHeartSentCounter            = 0;
static unsigned long lastHeartSentCounterSaveMs               = 0;
static constexpr unsigned long kHeartCounterSaveMinIntervalMs = 30000;

uint32_t calendarDaySinceEpochUtc(time_t utc) {
    if (utc < 0) {
        utc = 0;
    }
    return static_cast<uint32_t>(static_cast<uint64_t>(utc) / 86400ULL);
}

void loadCounterBaseline() {
    Preferences preferences;
    if (!preferences.begin("chaya", true)) {
        ESP_LOGW(TAG, "NVS chaya: Baseline lesen fehlgeschlagen, = 0");
        counterBaseline           = 0;
        sentCountBaseline         = 0;
        s_lastResetCalendarDayUtc = UINT32_MAX;
        return;
    }
    counterBaseline           = std::max<int32_t>(preferences.getInt("cntBase", 0), 0);
    sentCountBaseline         = std::max<int32_t>(preferences.getInt("sntBase", 0), 0);
    s_lastResetCalendarDayUtc = preferences.getUInt("rstDay", UINT32_MAX);
    preferences.end();
}

void configLoadWebAuthFromNvs() {
    Preferences prefs;
    if (!prefs.begin("cfg", true)) {
        s_webAuthEnabledCached = false;
        return;
    }
    s_webAuthEnabledCached = (prefs.getUChar(kNvAuthEn, 0) != 0);
    prefs.end();
}

bool configGetWebAuthEnabled() {
    return s_webAuthEnabledCached;
}

void configSetWebAuthEnabled(bool enabled) {
    Preferences prefs;
    if (!prefs.begin("cfg", false)) {
        ESP_LOGE(TAG, "NVS cfg: authEn schreiben fehlgeschlagen");
        return;
    }
    prefs.putUChar(kNvAuthEn, enabled ? 1 : 0);
    prefs.end();
    s_webAuthEnabledCached = enabled;
}

void configLoadResetPeriodFromNvs() {
    Preferences prefs;
    if (!prefs.begin("cfg", true)) {
        s_resetPeriodWeeklyCached = false;
        return;
    }
    s_resetPeriodWeeklyCached = (prefs.getUChar(kNvRstPeriod, 0) != 0);
    prefs.end();
}

bool configGetResetPeriodIsWeekly() {
    return s_resetPeriodWeeklyCached;
}

void configSetResetPeriodWeekly(bool weekly) {
    Preferences prefs;
    if (!prefs.begin("cfg", false)) {
        ESP_LOGE(TAG, "NVS cfg: rstPeriod schreiben fehlgeschlagen");
        return;
    }
    prefs.putUChar(kNvRstPeriod, weekly ? 1 : 0);
    prefs.end();
    s_resetPeriodWeeklyCached = weekly;
}

static bool persistCounterBaselineState() {
    Preferences preferences;
    if (!preferences.begin("chaya", false)) {
        ESP_LOGE(TAG, "NVS chaya: Baseline schreiben fehlgeschlagen");
        return false;
    }
    preferences.putInt("cntBase", counterBaseline);
    preferences.putInt("sntBase", sentCountBaseline);
    preferences.putUInt("rstDay", s_lastResetCalendarDayUtc);
    preferences.end();
    return true;
}

void maybePeriodicallyResetCounters() {
    if (configIsApMode()) {
        return;
    }
    const time_t utcNow = time(nullptr);
    if (!ntpTimeLooksSynced(utcNow)) {
        return;
    }
    const uint32_t currentDay = calendarDaySinceEpochUtc(utcNow);

    if (s_lastResetCalendarDayUtc == UINT32_MAX) {
        s_lastResetCalendarDayUtc = currentDay;
        if (!persistCounterBaselineState()) {
            s_lastResetCalendarDayUtc = UINT32_MAX;
        }
        return;
    }

    const bool weekly      = configGetResetPeriodIsWeekly();
    bool       shouldReset = false;
    if (weekly) {
        if (s_lastResetCalendarDayUtc <= currentDay
            && (currentDay - s_lastResetCalendarDayUtc) >= 7U) {
            shouldReset = true;
        }
    } else {
        shouldReset = (currentDay != s_lastResetCalendarDayUtc);
    }

    if (!shouldReset) {
        return;
    }

    counterBaseline           = heartCounter;
    sentCountBaseline         = heartSentCounter;
    s_lastResetCalendarDayUtc = currentDay;
    if (persistCounterBaselineState()) {
        ESP_LOGI(TAG, "Periodic display counter reset (%s)", weekly ? "weekly" : "daily");
        requestHeartRedraw();
    }
}

void loadHeartCounter() {
    Preferences preferences;
    if (!preferences.begin("chaya", true)) {
        ESP_LOGW(TAG, "NVS chaya: lesen fehlgeschlagen, Zaehler = 0");
        heartCounter                  = 0;
        heartSentCounter              = 0;
        lastCommittedHeartCounter     = 0;
        lastCommittedHeartSentCounter = 0;
        lastHeartCounterSaveMs        = millis();
        lastHeartSentCounterSaveMs    = millis();
        loadCounterBaseline();
        return;
    }
    heartCounter     = std::max<int32_t>(preferences.getInt("counter", 0), 0);
    heartSentCounter = std::max<int32_t>(preferences.getInt("sentCount", 0), 0);
    preferences.end();
    lastCommittedHeartCounter     = heartCounter;
    lastCommittedHeartSentCounter = heartSentCounter;
    lastHeartCounterSaveMs        = millis();
    lastHeartSentCounterSaveMs    = millis();
    loadCounterBaseline();
}

void loadHeartSentCounter() {
    Preferences preferences;
    if (!preferences.begin("chaya", true)) {
        ESP_LOGW(TAG, "NVS chaya: lesen sentCount fehlgeschlagen, = 0");
        heartSentCounter              = 0;
        lastCommittedHeartSentCounter = heartSentCounter;
        lastHeartSentCounterSaveMs    = millis();
        return;
    }
    heartSentCounter = std::max<int32_t>(preferences.getInt("sentCount", 0), 0);
    preferences.end();
    lastCommittedHeartSentCounter = heartSentCounter;
    lastHeartSentCounterSaveMs    = millis();
}

bool saveHeartCounter() {
    Preferences preferences;
    if (!preferences.begin("chaya", false)) {
        ESP_LOGE(TAG, "NVS chaya: schreiben fehlgeschlagen");
        return false;
    }
    preferences.putInt("counter", heartCounter);
    preferences.end();
    return true;
}

bool saveHeartSentCounter() {
    Preferences preferences;
    if (!preferences.begin("chaya", false)) {
        ESP_LOGE(TAG, "NVS chaya: schreiben sentCount fehlgeschlagen");
        return false;
    }
    preferences.putInt("sentCount", heartSentCounter);
    preferences.end();
    return true;
}

void maybeSaveHeartCounter() {
    if (heartCounter == lastCommittedHeartCounter) {
        return;
    }
    const unsigned long now = millis();
    if (now - lastHeartCounterSaveMs >= kHeartCounterSaveMinIntervalMs) {
        if (saveHeartCounter()) {
            lastCommittedHeartCounter = heartCounter;
            lastHeartCounterSaveMs    = now;
        }
    }
}

void maybeSaveHeartSentCounter() {
    if (heartSentCounter == lastCommittedHeartSentCounter) {
        return;
    }
    const unsigned long now = millis();
    if (now - lastHeartSentCounterSaveMs >= kHeartCounterSaveMinIntervalMs) {
        if (saveHeartSentCounter()) {
            lastCommittedHeartSentCounter = heartSentCounter;
            lastHeartSentCounterSaveMs    = now;
        }
    }
}

void flushHeartCounterIfDirty() {
    if (heartCounter != lastCommittedHeartCounter) {
        if (saveHeartCounter()) {
            lastCommittedHeartCounter = heartCounter;
            lastHeartCounterSaveMs    = millis();
        }
    }
}

void flushHeartSentCounterIfDirty() {
    if (heartSentCounter != lastCommittedHeartSentCounter) {
        if (saveHeartSentCounter()) {
            lastCommittedHeartSentCounter = heartSentCounter;
            lastHeartSentCounterSaveMs    = millis();
        }
    }
}

void counterResetRamAfterFactoryClear() {
    heartCounter                  = 0;
    heartSentCounter              = 0;
    counterBaseline               = 0;
    sentCountBaseline             = 0;
    s_lastResetCalendarDayUtc     = UINT32_MAX;
    lastCommittedHeartCounter     = 0;
    lastCommittedHeartSentCounter = 0;
    lastHeartCounterSaveMs        = millis();
    lastHeartSentCounterSaveMs    = millis();
    s_webAuthEnabledCached        = false;
}
