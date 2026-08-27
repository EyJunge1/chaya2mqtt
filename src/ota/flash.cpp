#include "flash.h"

#include "ota.h"
#include "ota_url_allow.h"
#include "tls/tls_bundle.h"
#include "tls/tls_bundle_setup.h"

#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
#include <Arduino.h>

#include "diag/task_watchdog.h"
#include "util/log_tag.h"

DEFINE_LOG_TAG("OTA");

namespace {

constexpr int kHttpClientTimeoutMs = 30000;

} // namespace

bool otaFlashVerifiedInstall(const char* binUrl, const char* sha256Url) {
    if (binUrl == nullptr || binUrl[0] == '\0' || sha256Url == nullptr
        || sha256Url[0] == '\0') {
        ESP_LOGE(TAG, "OTA firmware or SHA-256 sidecar URL missing");
        return false;
    }
    if (!otaReleaseDownloadUrlAllowed(binUrl, OtaDownloadAsset::Firmware)
        || !otaReleaseDownloadUrlAllowed(sha256Url, OtaDownloadAsset::Sha256)) {
        ESP_LOGE(TAG, "OTA download URL rejected by allowlist");
        return false;
    }

    if (!chayaTlsEnsureCaBundleInstalled()) {
        ESP_LOGE(TAG, "OTA: CA bundle install failed");
        return false;
    }

    WiFiClientSecure tls;
    tls.setCACertBundle(x509_crt_bundle_start,
                        static_cast<size_t>(x509_crt_bundle_end - x509_crt_bundle_start));
    tls.setTimeout(30000);

    HTTPUpdate updater(kHttpClientTimeoutMs);
    updater.rebootOnUpdate(false);
    updater.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    updater.setSHA256sumUrl(String(sha256Url));
    updater.onStart([]() {
        otaNotifyFlashProgress(0, 0);
        chayaTaskWatchdogReset();
    });
    updater.onProgress([](int current, int total) {
        const uint32_t done  = static_cast<uint32_t>(current < 0 ? 0 : current);
        const uint32_t bytes = static_cast<uint32_t>(total < 0 ? 0 : total);
        if (bytes > 0U && done >= bytes) {
            // HTTPUpdate calls Update.end() (including SHA-256 verification) after this.
            otaNotifyFlashVerifying();
        } else {
            otaNotifyFlashProgress(done, bytes);
        }
        chayaTaskWatchdogReset();
    });
    updater.onEnd([]() {
        chayaTaskWatchdogReset();
    });
    updater.onError([](int err) {
        ESP_LOGE(TAG, "HTTPUpdate error %d", err);
        chayaTaskWatchdogReset();
    });

    ESP_LOGI(TAG, "HTTPUpdate start: %s", binUrl);
    const t_httpUpdate_return ret = updater.update(tls, String(binUrl));
    chayaTaskWatchdogReset();
    if (ret != HTTP_UPDATE_OK) {
        ESP_LOGE(TAG, "HTTPUpdate failed: %s", updater.getLastErrorString().c_str());
        return false;
    }
    ESP_LOGI(TAG, "HTTPUpdate OK (SHA-256 verified)");
    return true;
}
