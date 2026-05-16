#include "counter.h"

#include "app_config.h"
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
        ESP_LOGI(TAG, "NVS chaya namespace not present yet, baselines = 0");
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

    const uint8_t periodDays = configGetResetPeriodDays();
    if (periodDays == 0U) {
        return;
    }

    const uint32_t daysSinceReset
        = (currentDay >= s_lastResetCalendarDayUtc) ? (currentDay - s_lastResetCalendarDayUtc) : 0U;
    const bool      shouldReset = (daysSinceReset >= static_cast<uint32_t>(periodDays));

    if (!shouldReset) {
        return;
    }

    counterBaseline           = heartCounter;
    sentCountBaseline         = heartSentCounter;
    s_lastResetCalendarDayUtc = currentDay;
    if (persistCounterBaselineState()) {
        ESP_LOGI(TAG, "Periodic display counter reset (%u days)", static_cast<unsigned>(periodDays));
        requestHeartRedraw();
    }
}

void maybeResetDisplayBaselinesWhenCapped() {
    if (configIsApMode()) {
        return;
    }
    bool changed = false;
    const int64_t dRecv = static_cast<int64_t>(heartCounter) - static_cast<int64_t>(counterBaseline);
    const int64_t dSent = static_cast<int64_t>(heartSentCounter) - static_cast<int64_t>(sentCountBaseline);
    if (dRecv >= kDisplayCounterMax) {
        counterBaseline = heartCounter;
        changed         = true;
    }
    if (dSent >= kDisplayCounterMax) {
        sentCountBaseline = heartSentCounter;
        changed           = true;
    }
    if (changed && persistCounterBaselineState()) {
        ESP_LOGI(TAG, "Display baseline reset (display reached cap)");
        requestHeartRedraw();
    }
}

void loadHeartCounter() {
    Preferences preferences;
    if (!preferences.begin("chaya", true)) {
        ESP_LOGI(TAG, "NVS chaya namespace not present yet, counters = 0");
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
    app_configResetRamAfterFactoryClear();
}
