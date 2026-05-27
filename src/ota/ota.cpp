#include "ota.h"

#include "github.h"
#include "flash.h"
#include "ota_task.h"

#include "config/app_config.h"
#include "config/nvs_keys.h"
#include "config/nvs_utils.h"
#include "constants.h"
#include "heart/counter.h"
#include "version.h"
#include "wifi/wlan.h"

#include <Arduino.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <esp_ota_ops.h>
#include <esp_partition.h>

#include "log_tag.h"

DEFINE_LOG_TAG("OTA");

constexpr size_t kOtaUrlMax = 256;

std::atomic<bool> g_otaCheckRequested{false};
std::atomic<bool> g_otaRequested{false};
std::atomic<bool> g_otaFlashInProgress{false};
std::atomic<bool> g_otaCheckInProgress{false};
char              g_otaUrl[kOtaUrlMax]{};

namespace {

static std::atomic<bool> s_otaHealthGateDone{false};

uint32_t s_cachedNvUpdateDay     = UINT32_MAX;
bool     s_nvUpdateDayCacheValid = false;

static bool otaPartitionPendingVerify() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (running == nullptr) {
        return false;
    }
    esp_ota_img_states_t imgState = ESP_OTA_IMG_UNDEFINED;
    if (esp_ota_get_state_partition(running, &imgState) != ESP_OK) {
        return false;
    }
    return imgState == ESP_OTA_IMG_PENDING_VERIFY;
}

static void otaMarkFirmwareValidIfPendingVerify() {
    if (!otaPartitionPendingVerify()) {
        return;
    }
    const esp_err_t v = esp_ota_mark_app_valid_cancel_rollback();
    if (v != ESP_OK) {
        ESP_LOGW(TAG, "esp_ota_mark_app_valid_cancel_rollback: %s", esp_err_to_name(v));
    } else {
        ESP_LOGI(TAG, "Firmware marked valid (rollback cancelled)");
    }
}

static void nvLoadLastUpdateCalendarDay(uint32_t* outDayUtc) {
    if (outDayUtc == nullptr) {
        return;
    }
    *outDayUtc = app_nvs::readUInt(kNvsNsCfg, kNvsKeyCfgUpdDay, 0);
}

static void nvSaveLastUpdateCalendarDay(uint32_t dayUtc) {
    if (!app_nvs::writeUInt(kNvsNsCfg, kNvsKeyCfgUpdDay, dayUtc)) {
        ESP_LOGE(TAG, "NVS cfg: failed to persist upd_day");
        return;
    }
    s_cachedNvUpdateDay     = dayUtc;
    s_nvUpdateDayCacheValid = true;
}

static void autoUpdateLoop() {
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
        g_otaCheckInProgress.store(true, std::memory_order_release);
        const GithubCheckResult gr = otaGithubEvaluateLatestRelease(fwUrl, sizeof(fwUrl));
        g_otaCheckInProgress.store(false, std::memory_order_release);
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
    g_otaCheckInProgress.store(true, std::memory_order_release);
    const GithubCheckResult gr = otaGithubEvaluateLatestRelease(fwUrl, sizeof(fwUrl));
    g_otaCheckInProgress.store(false, std::memory_order_release);
    if (gr == GithubCheckResult::ParsedUpgradeAvail) {
        strlcpy(g_otaUrl, fwUrl, sizeof(g_otaUrl));
        g_otaRequested.store(true, std::memory_order_release);
    }
    if (gr != GithubCheckResult::ApiError) {
        nvSaveLastUpdateCalendarDay(todayUtcDay);
    }
}

} // namespace

void otaTryMarkValidAfterHealthCheck() {
    if (s_otaHealthGateDone.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    if (!otaPartitionPendingVerify()) {
        return;
    }
    otaMarkFirmwareValidIfPendingVerify();
}

bool otaFlashInProgress() {
    return g_otaFlashInProgress.load(std::memory_order_acquire);
}

bool otaBlocksDestructiveAction() {
    return otaFlashInProgress() || g_otaCheckInProgress.load(std::memory_order_acquire);
}

void otaQueueGithubCheck() {
    g_otaCheckRequested.store(true, std::memory_order_release);
    otaTaskWake();
}

void otaLoop() {
    autoUpdateLoop();

    if (g_otaRequested.exchange(false, std::memory_order_acq_rel)) {
        flushHeartCounterIfDirty();
        flushHeartSentCounterIfDirty();

        char urlCopy[kOtaUrlMax];
        strlcpy(urlCopy, g_otaUrl, sizeof(urlCopy));

        g_otaFlashInProgress.store(true, std::memory_order_release);
        const bool installed = otaFlashVerifiedInstall(urlCopy);
        g_otaFlashInProgress.store(false, std::memory_order_release);

        if (!installed) {
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
