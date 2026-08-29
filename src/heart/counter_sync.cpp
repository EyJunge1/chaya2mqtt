#include "counter.h"
#include "counter_internal.h"

#include "config/app_config.h"
#include "config/nvs_utils.h"
#include "constants.h"
#include "display/display.h"
#include "display/display_config.h"
#include "util/time_helpers.h"
#include "wifi/wlan.h"

#include "util/log_tag.h"

#include <Arduino.h>
#include <Preferences.h>
#include <cinttypes>
#include <esp_log.h>

DEFINE_LOG_TAG("CTR");

bool persistCounterBaselineState() {
    if (!chayaNvsWritesAllowed()) {
        return false;
    }
    int      snapCntBase = 0;
    int      snapSntBase = 0;
    uint32_t snapRstDay  = UINT32_MAX;
    portENTER_CRITICAL(&s_heartDisplayMux);
    snapCntBase = counterBaseline.load(std::memory_order_relaxed);
    snapSntBase = sentCountBaseline.load(std::memory_order_relaxed);
    snapRstDay  = s_lastResetCalendarDayUtc.load(std::memory_order_relaxed);
    portEXIT_CRITICAL(&s_heartDisplayMux);

    app_nvs::ScopedNvsLock lock;
    Preferences            prefs;
    if (!prefs.begin(kNvsNsChaya, false)) {
        ESP_LOGE(TAG, "NVS chaya: open for baseline write failed");
        return false;
    }
    const ChayaBaselineBlob blob{snapCntBase, snapSntBase, snapRstDay};
    const bool okBlob =
        prefs.putBytes(kNvsKeyChayaBaselineBlob, &blob, sizeof(blob)) == sizeof(blob);
    prefs.end();
    if (!okBlob) {
        ESP_LOGE(TAG, "NVS chaya: baseline blob write failed");
        return false;
    }
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

    if (s_lastResetCalendarDayUtc.load(std::memory_order_relaxed) == UINT32_MAX) {
        s_lastResetCalendarDayUtc.store(currentDay, std::memory_order_relaxed);
        if (!persistCounterBaselineState()) {
            s_lastResetCalendarDayUtc.store(UINT32_MAX, std::memory_order_relaxed);
        }
        return;
    }

    const uint8_t periodDays = configGetResetPeriodDays();
    if (periodDays == 0U) {
        return;
    }

    const uint32_t lastDayRaw = s_lastResetCalendarDayUtc.load(std::memory_order_relaxed);
    uint32_t       lastDay    = lastDayRaw;
    if (lastDay != UINT32_MAX && lastDay > currentDay + 1U) {
        ESP_LOGW(TAG, "rstDay ahead of today — clamping anchor to today (%" PRIu32 ")", currentDay);
        lastDay = currentDay;
        s_lastResetCalendarDayUtc.store(lastDay, std::memory_order_relaxed);
    }
    const uint32_t daysSinceReset
        = (currentDay >= lastDay) ? (currentDay - lastDay) : 0U;
    const bool shouldReset = (daysSinceReset >= static_cast<uint32_t>(periodDays));

    if (!shouldReset) {
        static unsigned long s_lastNoResetLogMs = 0;
        const unsigned long  nowMs              = millis();
        if (s_lastNoResetLogMs == 0UL || (nowMs - s_lastNoResetLogMs) >= 300000UL) {
            s_lastNoResetLogMs = nowMs;
            ESP_LOGD(TAG,
                     "Periodic reset not due: %u / %u days since anchor day %" PRIu32 " (today %" PRIu32 ")",
                     static_cast<unsigned>(daysSinceReset), static_cast<unsigned>(periodDays), lastDay,
                     currentDay);
        }
        return;
    }

    portENTER_CRITICAL(&s_heartDisplayMux);
    counterBaseline.store(heartCounter.load(std::memory_order_relaxed), std::memory_order_relaxed);
    sentCountBaseline.store(heartSentCounter.load(std::memory_order_relaxed), std::memory_order_relaxed);
    portEXIT_CRITICAL(&s_heartDisplayMux);
    s_lastResetCalendarDayUtc.store(currentDay, std::memory_order_relaxed);
    if (persistCounterBaselineState()) {
        ESP_LOGI(TAG, "Periodic display counter reset (%u days)", static_cast<unsigned>(periodDays));
        (void)displayRequest(DisplayMsg::Cmd::DrawHeart, DisplayRequestMode::Content);
    }
}

void maybeResetDisplayBaselinesWhenCapped() {
    if (configIsApMode()) {
        return;
    }
    bool    changed   = false;
    int32_t snapHeart = 0;
    int32_t snapSent  = 0;
    int32_t snapCb    = 0;
    int32_t snapSb    = 0;
    portENTER_CRITICAL(&s_heartDisplayMux);
    snapHeart = heartCounter.load(std::memory_order_relaxed);
    snapSent  = heartSentCounter.load(std::memory_order_relaxed);
    snapCb    = counterBaseline.load(std::memory_order_relaxed);
    snapSb    = sentCountBaseline.load(std::memory_order_relaxed);
    auto applyCapBaseline = [&](int32_t counter, int32_t baseline, std::atomic<int>& baselineAtom) {
        if (static_cast<int64_t>(counter) - static_cast<int64_t>(baseline) >= kDisplayCounterMax) {
            baselineAtom.store(counter, std::memory_order_relaxed);
            changed = true;
        }
    };
    applyCapBaseline(snapHeart, snapCb, counterBaseline);
    applyCapBaseline(snapSent, snapSb, sentCountBaseline);
    portEXIT_CRITICAL(&s_heartDisplayMux);
    if (changed && persistCounterBaselineState()) {
        ESP_LOGI(TAG, "Display baseline reset (display reached cap)");
        (void)displayRequest(DisplayMsg::Cmd::DrawHeart, DisplayRequestMode::Content);
    }
}
