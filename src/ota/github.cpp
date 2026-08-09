#include "github.h"

#include "github_parse.h"
#include "version_cmp.h"

#include "tls/tls_bundle_setup.h"
#include "config/version.h"
#include "wifi/wlan.h"

#include "tls/tls_bundle.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <Arduino.h>
#include <esp_log.h>

#include "diag/task_watchdog.h"
#include "util/log_tag.h"

DEFINE_LOG_TAG("OTA");

namespace {

constexpr const char kGithubLatestReleaseApiUrl[] =
    "https://api.github.com/repos/EyJunge1/chaya2mqtt/releases/latest";

constexpr const char kGithubReleasesListApiUrl[] =
    "https://api.github.com/repos/EyJunge1/chaya2mqtt/releases?per_page=10";

constexpr const char kGithubDownloadBase[] =
    "https://github.com/EyJunge1/chaya2mqtt/releases/download/";

constexpr size_t kGithubJsonBuf = 16384;

char s_githubJsonBuf[kGithubJsonBuf];

void stripLeadingV(const char* tag, char* out, size_t outLen) {
    if (out == nullptr || outLen == 0U) {
        return;
    }
    out[0] = '\0';
    if (tag == nullptr || tag[0] == '\0') {
        return;
    }
    const char* p = tag;
    if (p[0] == 'v' || p[0] == 'V') {
        ++p;
    }
    strlcpy(out, p, outLen);
}

bool fillReleaseUrls(const char* tag, OtaReleaseInfo* out) {
    if (tag == nullptr || tag[0] == '\0' || out == nullptr) {
        return false;
    }
    strlcpy(out->tag, tag, sizeof(out->tag));
    stripLeadingV(tag, out->version, sizeof(out->version));
    const int nBin = snprintf(out->binUrl, sizeof(out->binUrl), "%s%s/firmware.bin",
                              kGithubDownloadBase, tag);
    const int nMd5 = snprintf(out->md5Url, sizeof(out->md5Url), "%s%s/firmware.md5",
                              kGithubDownloadBase, tag);
    return nBin > 0 && static_cast<size_t>(nBin) < sizeof(out->binUrl) && nMd5 > 0
           && static_cast<size_t>(nMd5) < sizeof(out->md5Url);
}

bool httpGetGithubJson(const char* url, size_t* outLen) {
    if (url == nullptr || outLen == nullptr) {
        return false;
    }
    *outLen = 0;
    s_githubJsonBuf[0] = '\0';

    if (!wlanStaConnectedOk()) {
        ESP_LOGW(TAG, "GitHub update: STA Wi-Fi not connected");
        return false;
    }
    if (!chayaTlsEnsureCaBundleInstalled()) {
        ESP_LOGE(TAG, "GitHub API: CA bundle install failed");
        return false;
    }

    WiFiClientSecure tls;
    tls.setCACertBundle(x509_crt_bundle_start,
                        static_cast<size_t>(x509_crt_bundle_end - x509_crt_bundle_start));
    tls.setTimeout(30000);

    HTTPClient https;
    if (!https.begin(tls, url)) {
        ESP_LOGE(TAG, "GitHub API: HTTPS begin failed");
        return false;
    }
    https.setConnectTimeout(15000);
    https.setTimeout(30000);
    https.addHeader(F("User-Agent"), F("Chaya2MQTT-esp32"));
    https.addHeader(F("Accept"), F("application/vnd.github+json"));

    const int httpCode = https.GET();
    chayaTaskWatchdogReset();
    if (httpCode != HTTP_CODE_OK) {
        ESP_LOGE(TAG, "GitHub API: HTTP error %d", httpCode);
        https.end();
        return false;
    }

    auto&               stream        = https.getStream();
    size_t              len           = 0;
    const unsigned long streamStartMs = millis();
    constexpr unsigned long kGithubStreamDeadlineMs = 45000UL;

    while (https.connected() && len + 1 < kGithubJsonBuf
           && (millis() - streamStartMs) < kGithubStreamDeadlineMs) {
        chayaTaskWatchdogReset();
        if (stream.available() <= 0) {
            if (!https.connected()) {
                break;
            }
            delay(10);
            continue;
        }
        const int toRead =
            std::min(static_cast<int>(kGithubJsonBuf - 1 - len), stream.available());
        if (toRead <= 0) {
            break;
        }
        const int n = stream.readBytes(s_githubJsonBuf + len, toRead);
        if (n <= 0) {
            break;
        }
        len += static_cast<size_t>(n);
    }
    https.end();
    s_githubJsonBuf[len] = '\0';
    *outLen              = len;
    return len > 0U;
}

bool evaluateTag(const char* tag, bool isPrerelease, OtaChannel channel, OtaReleaseInfo* out,
                 GithubCheckResult* result) {
    if (tag == nullptr || out == nullptr || result == nullptr) {
        return false;
    }
    if (!fillReleaseUrls(tag, out)) {
        *result = GithubCheckResult::ApiError;
        return false;
    }
    out->channel       = channel;
    out->isPrerelease  = isPrerelease;

    ESP_LOGI(TAG, "GitHub channel=%s tag=%s local=%s",
             channel == OtaChannel::Beta ? "beta" : "stable", tag, APP_VERSION);

    if (!otaVersionIsNewer(tag, APP_VERSION)) {
        *result = GithubCheckResult::ParsedNoUpgrade;
        return true;
    }
    *result = GithubCheckResult::ParsedUpgradeAvail;
    return true;
}

} // namespace

GithubCheckResult otaGithubEvaluateChannel(OtaChannel channel, OtaReleaseInfo* out) {
    if (out == nullptr) {
        return GithubCheckResult::ApiError;
    }
    *out = OtaReleaseInfo{};
    out->channel = channel;

    size_t len = 0;
    if (channel == OtaChannel::Stable) {
        if (!httpGetGithubJson(kGithubLatestReleaseApiUrl, &len)) {
            return GithubCheckResult::ApiError;
        }
        char tag[64]{};
        if (!otaParseJsonStringField(s_githubJsonBuf, "tag_name", tag, sizeof(tag))) {
            ESP_LOGE(TAG, "GitHub: failed to parse tag_name");
            return GithubCheckResult::ApiError;
        }
        bool draft = false;
        (void)otaParseJsonBoolField(s_githubJsonBuf, "draft", &draft);
        if (draft) {
            ESP_LOGW(TAG, "GitHub latest is draft — skipping");
            return GithubCheckResult::ParsedNoUpgrade;
        }
        if (!otaJsonHasAssetName(s_githubJsonBuf, "firmware.bin")
            || !otaJsonHasAssetName(s_githubJsonBuf, "firmware.md5")) {
            // Assets may appear later in a truncated buffer; still allow if tag parses.
            ESP_LOGW(TAG, "GitHub latest: firmware assets not seen in JSON prefix");
        }
        GithubCheckResult result = GithubCheckResult::ApiError;
        if (!evaluateTag(tag, false, channel, out, &result)) {
            return GithubCheckResult::ApiError;
        }
        return result;
    }

    // Beta: prefer newest prerelease; fall back to newest stable in the list.
    if (!httpGetGithubJson(kGithubReleasesListApiUrl, &len)) {
        return GithubCheckResult::ApiError;
    }
    char tag[64]{};
    bool isPre = false;
    if (!otaSelectReleaseFromListJson(s_githubJsonBuf, true, tag, sizeof(tag), &isPre)) {
        ESP_LOGE(TAG, "GitHub: no suitable beta/stable release in list");
        return GithubCheckResult::ApiError;
    }
    GithubCheckResult result = GithubCheckResult::ApiError;
    if (!evaluateTag(tag, isPre, channel, out, &result)) {
        return GithubCheckResult::ApiError;
    }
    return result;
}
