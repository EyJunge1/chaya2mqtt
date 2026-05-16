#include "ota.h"

#include "github.h"
#include "flash.h"

#include "config/app_config.h"
#include "constants.h"
#include "heart/counter.h"
#include "version.h"
#include "wifi/wlan.h"
#include "config/nvs_utils.h"

#include <Arduino.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <ctime>

#include <esp_log.h>

#include "log_tag.h"

DEFINE_LOG_TAG("OTA");

namespace {

constexpr size_t      kOtaUrlMax               = 256;
constexpr const char  kNvCfgNs[]               = "cfg";
constexpr const char  kNvKeyUpdateCalendarDay[] = "upd_day";

std::atomic<bool> g_otaCheckRequested{false};
std::atomic<bool> g_otaRequested{false};
char               g_otaUrl[kOtaUrlMax]{};

uint32_t s_cachedNvUpdateDay     = UINT32_MAX;
bool     s_nvUpdateDayCacheValid = false;

void nvLoadLastUpdateCalendarDay(uint32_t* outDayUtc) {
    if (outDayUtc == nullptr) {
        return;
    }
    *outDayUtc = app_nvs::readUInt(kNvCfgNs, kNvKeyUpdateCalendarDay, 0);
}

void nvSaveLastUpdateCalendarDay(uint32_t dayUtc) {
    if (!app_nvs::writeUInt(kNvCfgNs, kNvKeyUpdateCalendarDay, dayUtc)) {
        ESP_LOGE(TAG, "NVS cfg: failed to persist upd_day");
        return;
    }
    s_cachedNvUpdateDay     = dayUtc;
    s_nvUpdateDayCacheValid = true;
}

void autoUpdateLoop() {
    if (configIsApMode()) {
        return;
    }
    if (!wlanStaConnectedOk()) {
        return;
    }

    const time_t   utcNow      = time(nullptr);
    const uint32_t todayUtcDay = calendarDaySinceEpochUtc(utcNow > 0 ? utcNow : 0);

    if (g_otaCheckRequested.exchange(false, std::memory_order_acq_rel)) {
        const bool ntpOk = ntpTimeLooksSynced(utcNow);
        if (!ntpOk) {
            ESP_LOGW(
                TAG,
                "Manual update check: wall-clock not plausible (NTP?) — checking GitHub anyway");
        }
        char fwUrl[kOtaUrlMax]{};
        const GithubCheckResult gr = otaGithubEvaluateLatestRelease(fwUrl, sizeof(fwUrl));
        if (gr != GithubCheckResult::ApiError) {
            if (gr == GithubCheckResult::ParsedUpgradeAvail) {
                strlcpy(g_otaUrl, fwUrl, sizeof(g_otaUrl));
                g_otaRequested.store(true, std::memory_order_release);
            }
        }
        if (ntpOk && gr != GithubCheckResult::ApiError) {
            nvSaveLastUpdateCalendarDay(todayUtcDay);
        }
        return;
    }

    if (strcmp(APP_VERSION, "dev") == 0) {
        return;
    }
    if (!ntpTimeLooksSynced(utcNow)) {
        return;
    }

    if (!s_nvUpdateDayCacheValid) {
        nvLoadLastUpdateCalendarDay(&s_cachedNvUpdateDay);
        s_nvUpdateDayCacheValid = true;
    }
    if (s_cachedNvUpdateDay == todayUtcDay) {
        return;
    }

    char fwUrl[kOtaUrlMax]{};
    const GithubCheckResult gr = otaGithubEvaluateLatestRelease(fwUrl, sizeof(fwUrl));
    if (gr == GithubCheckResult::ParsedUpgradeAvail) {
        strlcpy(g_otaUrl, fwUrl, sizeof(g_otaUrl));
        g_otaRequested.store(true, std::memory_order_release);
    }
    if (gr != GithubCheckResult::ApiError) {
        nvSaveLastUpdateCalendarDay(todayUtcDay);
    }
}

} // namespace

void otaQueueGithubCheck() {
    g_otaCheckRequested.store(true, std::memory_order_release);
}

void otaLoop() {
    autoUpdateLoop();

    if (g_otaRequested.exchange(false, std::memory_order_acq_rel)) {
        flushHeartCounterIfDirty();
        flushHeartSentCounterIfDirty();

        char urlCopy[kOtaUrlMax];
        strlcpy(urlCopy, g_otaUrl, sizeof(urlCopy));

        if (!otaFlashVerifiedInstall(urlCopy)) {
            ESP_LOGE(TAG, "OTA aborted (no reboot)");
            return;
        }

        flushHeartCounterIfDirty();
        flushHeartSentCounterIfDirty();
        delay(200);
        releaseGpioHoldBeforeRestart();
        ESP.restart();
    }
}
