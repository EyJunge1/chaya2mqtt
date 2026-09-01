#include "flash.h"

#include "ota.h"
#include "ota_url_allow.h"
#include "tls/tls_bundle.h"
#include "tls/tls_bundle_setup.h"

#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
#include <Arduino.h>

#include "diag/task_watchdog.h"
#include "util/log_tag.h"

#include <cstdio>
#include <cstring>

DEFINE_LOG_TAG("OTA");

namespace {

constexpr int    kHttpClientTimeoutMs = 30000;
constexpr int    kOtaMaxRedirects     = 5;
constexpr size_t kOtaResolvedUrlBytes = 768U;

bool isHttpRedirect(int code) {
    return code == HTTP_CODE_MOVED_PERMANENTLY || code == HTTP_CODE_FOUND
           || code == HTTP_CODE_SEE_OTHER || code == HTTP_CODE_TEMPORARY_REDIRECT
           || code == 308;
}

/** Resolve relative Location against the current absolute URL into out. */
bool resolveHttpLocation(const char* baseUrl, const String& location, char* out, size_t outLen) {
    if (out == nullptr || outLen == 0U || baseUrl == nullptr) {
        return false;
    }
    out[0] = '\0';
    if (location.length() == 0U) {
        return false;
    }
    if (location.startsWith("https://") || location.startsWith("http://")) {
        if (static_cast<size_t>(location.length()) >= outLen) {
            return false;
        }
        strlcpy(out, location.c_str(), outLen);
        return true;
    }
    // Absolute path on same origin.
    if (location.startsWith("/")) {
        const char* schemeEnd = strstr(baseUrl, "://");
        if (schemeEnd == nullptr) {
            return false;
        }
        const char* hostStart = schemeEnd + 3;
        const char* pathStart = strchr(hostStart, '/');
        const size_t originLen =
            (pathStart != nullptr) ? static_cast<size_t>(pathStart - baseUrl) : strlen(baseUrl);
        if (originLen + location.length() >= outLen) {
            return false;
        }
        memcpy(out, baseUrl, originLen);
        out[originLen] = '\0';
        strlcat(out, location.c_str(), outLen);
        return true;
    }
    // Relative path: replace final path segment.
    const char* slash = strrchr(baseUrl, '/');
    if (slash == nullptr) {
        return false;
    }
    const size_t prefixLen = static_cast<size_t>(slash - baseUrl + 1);
    if (prefixLen + location.length() >= outLen) {
        return false;
    }
    memcpy(out, baseUrl, prefixLen);
    out[prefixLen] = '\0';
    strlcat(out, location.c_str(), outLen);
    return true;
}

bool isGithubReleaseDownloadUrl(const char* url) {
    return otaReleaseDownloadUrlAllowed(url, OtaDownloadAsset::Firmware)
           || otaReleaseDownloadUrlAllowed(url, OtaDownloadAsset::Sha256);
}

/**
 * HEAD GitHub release URLs only. Parse each Location and check the allowlist (SEC-11).
 * Stop at the first CDN hop — do not probe the signed URL (expiry / no GET confirm).
 */
bool otaResolveDownloadUrl(WiFiClientSecure& tls, const char* startUrl, OtaDownloadAsset asset,
                           char* outUrl, size_t outLen) {
    if (startUrl == nullptr || outUrl == nullptr || outLen == 0U
        || !otaReleaseDownloadUrlAllowed(startUrl, asset)) {
        return false;
    }
    if (strlen(startUrl) >= outLen) {
        return false;
    }
    strlcpy(outUrl, startUrl, outLen);

    for (int hop = 0; hop <= kOtaMaxRedirects; ++hop) {
        chayaTaskWatchdogReset();
        HTTPClient https;
        if (!https.begin(tls, outUrl)) {
            ESP_LOGE(TAG, "OTA URL resolve: begin failed");
            return false;
        }
        https.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
        https.setReuse(false);
        https.setConnectTimeout(15000);
        https.setTimeout(kHttpClientTimeoutMs);
        https.addHeader(F("User-Agent"), F("Chaya2MQTT-esp32"));

        const int code = https.sendRequest("HEAD");

        if (isHttpRedirect(code)) {
            const String location = https.getLocation();
            https.end();
            char next[kOtaResolvedUrlBytes]{};
            if (!resolveHttpLocation(outUrl, location, next, sizeof(next))) {
                ESP_LOGE(TAG, "OTA redirect Location unusable (hop %d)", hop);
                return false;
            }
            if (!otaReleaseDownloadRedirectUrlAllowed(next)) {
                ESP_LOGE(TAG, "OTA redirect rejected by allowlist: %s", next);
                return false;
            }
            if (hop == kOtaMaxRedirects) {
                ESP_LOGE(TAG, "OTA too many redirects");
                return false;
            }
            strlcpy(outUrl, next, outLen);
            if (!isGithubReleaseDownloadUrl(outUrl)) {
                return true;
            }
            continue;
        }

        https.end();
        if (code == HTTP_CODE_OK || code == HTTP_CODE_PARTIAL_CONTENT) {
            return otaReleaseDownloadRedirectUrlAllowed(outUrl);
        }

        ESP_LOGE(TAG, "OTA URL resolve HTTP %d for %s", code, outUrl);
        return false;
    }
    return false;
}

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

    char resolvedBin[kOtaResolvedUrlBytes]{};
    char resolvedSha[kOtaResolvedUrlBytes]{};
    if (!otaResolveDownloadUrl(tls, binUrl, OtaDownloadAsset::Firmware, resolvedBin,
                               sizeof(resolvedBin))) {
        ESP_LOGE(TAG, "OTA firmware URL redirect resolve failed");
        return false;
    }
    if (!otaResolveDownloadUrl(tls, sha256Url, OtaDownloadAsset::Sha256, resolvedSha,
                               sizeof(resolvedSha))) {
        ESP_LOGE(TAG, "OTA SHA-256 URL redirect resolve failed");
        return false;
    }

    HTTPUpdate updater(kHttpClientTimeoutMs);
    updater.rebootOnUpdate(false);
    // Redirects already resolved and allowlisted; do not follow further opaque hops.
    updater.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    // HTTPUpdate API requires Arduino String; URLs already resolved into stack buffers (STAB-07).
    updater.setSHA256sumUrl(String(resolvedSha));
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

    ESP_LOGI(TAG, "HTTPUpdate start: %s", resolvedBin);
    const t_httpUpdate_return ret = updater.update(tls, String(resolvedBin));
    chayaTaskWatchdogReset();
    if (ret != HTTP_UPDATE_OK) {
        ESP_LOGE(TAG, "HTTPUpdate failed: %s", updater.getLastErrorString().c_str());
        return false;
    }
    ESP_LOGI(TAG, "HTTPUpdate OK (SHA-256 verified)");
    return true;
}
