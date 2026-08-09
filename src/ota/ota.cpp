#include "ota.h"

#include "github.h"
#include "flash.h"
#include "ota_task.h"

#include "config/app_config.h"
#include "config/nvs_keys.h"
#include "config/nvs_utils.h"
#include "constants.h"
#include "heart/counter.h"
#include "config/version.h"
#include "wifi/wlan.h"

#include <Arduino.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <esp_ota_ops.h>
#include <esp_partition.h>

#include "util/log_tag.h"

DEFINE_LOG_TAG("OTA");

namespace {

std::atomic<bool> g_otaCheckRequested{false};
std::atomic<bool> g_otaInstallRequested{false};
std::atomic<bool> g_otaFlashInProgress{false};
std::atomic<bool> g_otaCheckInProgress{false};
std::atomic<bool> s_otaHealthGateDone{false};

portMUX_TYPE s_otaMux = portMUX_INITIALIZER_UNLOCKED;

OtaStatus      s_status{};
OtaReleaseInfo s_pendingRelease{};
bool           s_havePendingRelease = false;

uint32_t   s_cachedNvUpdateDay     = UINT32_MAX;
bool       s_nvUpdateDayCacheValid = false;
bool       s_channelLoaded         = false;
OtaChannel s_channel               = OtaChannel::Stable;

const char* phaseName(OtaPhase p) {
    switch (p) {
        case OtaPhase::Idle:
            return "idle";
        case OtaPhase::Checking:
            return "checking";
        case OtaPhase::Available:
            return "available";
        case OtaPhase::Downloading:
            return "downloading";
        case OtaPhase::Verifying:
            return "verifying";
        case OtaPhase::Rebooting:
            return "rebooting";
        case OtaPhase::Error:
            return "error";
        default:
            return "idle";
    }
}

const char* channelName(OtaChannel c) {
    return c == OtaChannel::Beta ? "beta" : "stable";
}

void bumpLocked() {
    ++s_status.generation;
}

void setPhaseLocked(OtaPhase phase) {
    s_status.phase = phase;
    bumpLocked();
}

void setErrorLocked(const char* code) {
    s_status.phase = OtaPhase::Error;
    strlcpy(s_status.error, code != nullptr ? code : "error", sizeof(s_status.error));
    s_status.bytesDone  = 0;
    s_status.bytesTotal = 0;
    bumpLocked();
}

void ensureLocalVersionLocked() {
    if (s_status.localVersion[0] == '\0') {
        strlcpy(s_status.localVersion, APP_VERSION, sizeof(s_status.localVersion));
    }
}

void loadChannelIfNeeded() {
    if (s_channelLoaded) {
        return;
    }
    char buf[16]{};
    (void)app_nvs::readString(kNvsNsCfg, kNvsKeyCfgUpdChan, buf, sizeof(buf));
    s_channel       = (strcmp(buf, "beta") == 0) ? OtaChannel::Beta : OtaChannel::Stable;
    s_channelLoaded = true;
    portENTER_CRITICAL(&s_otaMux);
    s_status.channel = s_channel;
    ensureLocalVersionLocked();
    portEXIT_CRITICAL(&s_otaMux);
}

bool otaPartitionPendingVerify() {
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

void otaMarkFirmwareValidIfPendingVerify() {
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

void nvLoadLastUpdateCalendarDay(uint32_t* outDayUtc) {
    if (outDayUtc == nullptr) {
        return;
    }
    *outDayUtc = app_nvs::readUInt(kNvsNsCfg, kNvsKeyCfgUpdDay, 0);
}

void nvSaveLastUpdateCalendarDay(uint32_t dayUtc) {
    if (!app_nvs::writeUInt(kNvsNsCfg, kNvsKeyCfgUpdDay, dayUtc)) {
        ESP_LOGE(TAG, "NVS cfg: failed to persist upd_day");
        return;
    }
    s_cachedNvUpdateDay     = dayUtc;
    s_nvUpdateDayCacheValid = true;
}

bool isBusyPhase(OtaPhase p) {
    return p == OtaPhase::Checking || p == OtaPhase::Downloading || p == OtaPhase::Verifying
           || p == OtaPhase::Rebooting;
}

void runGithubCheck(bool manual) {
    loadChannelIfNeeded();

    portENTER_CRITICAL(&s_otaMux);
    if (isBusyPhase(s_status.phase) && s_status.phase != OtaPhase::Checking) {
        portEXIT_CRITICAL(&s_otaMux);
        ESP_LOGW(TAG, "OTA check skipped — busy");
        return;
    }
    s_havePendingRelease         = false;
    s_pendingRelease             = OtaReleaseInfo{};
    s_status.availableVersion[0] = '\0';
    s_status.error[0]            = '\0';
    s_status.bytesDone           = 0;
    s_status.bytesTotal          = 0;
    s_status.channel             = s_channel;
    ensureLocalVersionLocked();
    setPhaseLocked(OtaPhase::Checking);
    portEXIT_CRITICAL(&s_otaMux);

    g_otaCheckInProgress.store(true, std::memory_order_release);
    OtaReleaseInfo info{};
    const GithubCheckResult gr = otaGithubEvaluateChannel(s_channel, &info);
    g_otaCheckInProgress.store(false, std::memory_order_release);

    portENTER_CRITICAL(&s_otaMux);
    s_status.channel = s_channel;
    ensureLocalVersionLocked();
    if (gr == GithubCheckResult::ApiError) {
        setErrorLocked("api_error");
    } else if (gr == GithubCheckResult::ParsedUpgradeAvail) {
        s_pendingRelease     = info;
        s_havePendingRelease = true;
        strlcpy(s_status.availableVersion, info.version, sizeof(s_status.availableVersion));
        setPhaseLocked(OtaPhase::Available);
    } else {
        s_havePendingRelease         = false;
        s_status.availableVersion[0] = '\0';
        setPhaseLocked(OtaPhase::Idle);
    }
    portEXIT_CRITICAL(&s_otaMux);

    if (manual) {
        ESP_LOGI(TAG, "Manual update check done (%s)",
                 gr == GithubCheckResult::ParsedUpgradeAvail
                     ? "available"
                     : (gr == GithubCheckResult::ApiError ? "error" : "up_to_date"));
    }
}

void runInstall() {
    OtaReleaseInfo release{};
    portENTER_CRITICAL(&s_otaMux);
    if (!s_havePendingRelease
        || (s_status.phase != OtaPhase::Available && s_status.phase != OtaPhase::Error)) {
        portEXIT_CRITICAL(&s_otaMux);
        ESP_LOGW(TAG, "OTA install ignored — no pending release");
        return;
    }
    release             = s_pendingRelease;
    s_status.error[0]   = '\0';
    s_status.bytesDone  = 0;
    s_status.bytesTotal = 0;
    setPhaseLocked(OtaPhase::Downloading);
    portEXIT_CRITICAL(&s_otaMux);

    flushHeartCounterIfDirty();
    flushHeartSentCounterIfDirty();

    g_otaFlashInProgress.store(true, std::memory_order_release);
    const bool installed = otaFlashVerifiedInstall(release.binUrl, release.md5Url);
    g_otaFlashInProgress.store(false, std::memory_order_release);

    if (!installed) {
        portENTER_CRITICAL(&s_otaMux);
        setErrorLocked("install_failed");
        if (s_havePendingRelease) {
            strlcpy(s_status.availableVersion, s_pendingRelease.version,
                    sizeof(s_status.availableVersion));
        }
        portEXIT_CRITICAL(&s_otaMux);
        ESP_LOGE(TAG, "OTA aborted (no reboot)");
        return;
    }

    portENTER_CRITICAL(&s_otaMux);
    setPhaseLocked(OtaPhase::Rebooting);
    portEXIT_CRITICAL(&s_otaMux);

    flushHeartCounterIfDirty();
    flushHeartSentCounterIfDirty();
    delay(200);
    releaseGpioHoldBeforeRestart();
    ESP.restart();
}

void maybeDailyCheck() {
    if (configIsApMode() || !wlanStaConnectedOk()) {
        return;
    }
    if (strcmp(APP_VERSION, "dev") == 0) {
        return;
    }
    const time_t utcNow = time(nullptr);
    if (!ntpTimeLooksSynced(utcNow)) {
        return;
    }

    const uint32_t todayUtcDay = calendarDaySinceEpochUtc(utcNow > 0 ? utcNow : 0);
    if (!s_nvUpdateDayCacheValid) {
        nvLoadLastUpdateCalendarDay(&s_cachedNvUpdateDay);
        s_nvUpdateDayCacheValid = true;
    }
    if (s_cachedNvUpdateDay == todayUtcDay) {
        return;
    }

    portENTER_CRITICAL(&s_otaMux);
    const bool busy = isBusyPhase(s_status.phase);
    portEXIT_CRITICAL(&s_otaMux);
    if (busy) {
        return;
    }

    // Daily check only — never auto-install.
    runGithubCheck(false);
    portENTER_CRITICAL(&s_otaMux);
    const bool ok = s_status.phase != OtaPhase::Error;
    portEXIT_CRITICAL(&s_otaMux);
    if (ok) {
        nvSaveLastUpdateCalendarDay(todayUtcDay);
    }
}

} // namespace

void otaNotifyFlashProgress(uint32_t done, uint32_t total) {
    portENTER_CRITICAL(&s_otaMux);
    s_status.phase      = OtaPhase::Downloading;
    s_status.bytesDone  = done;
    s_status.bytesTotal = total;
    bumpLocked();
    portEXIT_CRITICAL(&s_otaMux);
}

void otaNotifyFlashVerifying() {
    portENTER_CRITICAL(&s_otaMux);
    setPhaseLocked(OtaPhase::Verifying);
    portEXIT_CRITICAL(&s_otaMux);
}

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

bool otaSetChannel(OtaChannel channel) {
    loadChannelIfNeeded();
    const char* name = channelName(channel);
    if (!app_nvs::writeString(kNvsNsCfg, kNvsKeyCfgUpdChan, name)) {
        ESP_LOGE(TAG, "NVS cfg: failed to persist upd_chan");
        return false;
    }
    s_channel       = channel;
    s_channelLoaded = true;
    portENTER_CRITICAL(&s_otaMux);
    s_status.channel = channel;
    bumpLocked();
    portEXIT_CRITICAL(&s_otaMux);
    return true;
}

OtaChannel otaGetChannel() {
    loadChannelIfNeeded();
    return s_channel;
}

void otaCopyStatus(OtaStatus* out) {
    if (out == nullptr) {
        return;
    }
    loadChannelIfNeeded();
    portENTER_CRITICAL(&s_otaMux);
    ensureLocalVersionLocked();
    *out = s_status;
    portEXIT_CRITICAL(&s_otaMux);
}

size_t otaFormatStatusJson(char* buf, size_t bufLen) {
    if (buf == nullptr || bufLen < 32U) {
        return 0;
    }
    OtaStatus st{};
    otaCopyStatus(&st);
    const int n = snprintf(
        buf, bufLen,
        "{\"phase\":\"%s\",\"channel\":\"%s\",\"localVersion\":\"%s\","
        "\"availableVersion\":\"%s\",\"bytesDone\":%u,\"bytesTotal\":%u,"
        "\"error\":\"%s\",\"generation\":%u}",
        phaseName(st.phase), channelName(st.channel), st.localVersion, st.availableVersion,
        static_cast<unsigned>(st.bytesDone), static_cast<unsigned>(st.bytesTotal), st.error,
        static_cast<unsigned>(st.generation));
    if (n < 0 || static_cast<size_t>(n) >= bufLen) {
        return 0;
    }
    return static_cast<size_t>(n);
}

void otaQueueGithubCheck() {
    loadChannelIfNeeded();
    g_otaCheckRequested.store(true, std::memory_order_release);
    otaTaskWake();
}

void otaQueueGithubCheck(OtaChannel channel) {
    (void)otaSetChannel(channel);
    otaQueueGithubCheck();
}

void otaQueueInstall() {
    g_otaInstallRequested.store(true, std::memory_order_release);
    otaTaskWake();
}

void otaLoop() {
    if (g_otaCheckRequested.exchange(false, std::memory_order_acq_rel)) {
        const time_t utcNow = time(nullptr);
        const bool   ntpOk  = ntpTimeLooksSynced(utcNow);
        if (!ntpOk) {
            ESP_LOGW(TAG,
                     "Manual update check: wall-clock not plausible (NTP?) — checking GitHub "
                     "anyway");
        }
        runGithubCheck(true);
        if (ntpOk && wlanStaConnectedOk() && !configIsApMode()) {
            portENTER_CRITICAL(&s_otaMux);
            const bool ok = s_status.phase != OtaPhase::Error;
            portEXIT_CRITICAL(&s_otaMux);
            if (ok) {
                const uint32_t todayUtcDay =
                    calendarDaySinceEpochUtc(utcNow > 0 ? utcNow : 0);
                nvSaveLastUpdateCalendarDay(todayUtcDay);
            }
        }
    }

    if (g_otaInstallRequested.exchange(false, std::memory_order_acq_rel)) {
        runInstall();
        return;
    }

    maybeDailyCheck();
}
