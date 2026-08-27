#include "github.h"

#include "github_parse.h"
#include "ota_url_allow.h"
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
    "https://api.github.com/repos/EyJunge1/chaya2mqtt/releases?per_page=1&page=";

constexpr const char kGithubReleaseByTagApiBase[] =
    "https://api.github.com/repos/EyJunge1/chaya2mqtt/releases/tags/";

constexpr const char kGithubDownloadBase[] =
    "https://github.com/EyJunge1/chaya2mqtt/releases/download/";

constexpr size_t kGithubJsonBuf = 16384;
constexpr unsigned kGithubMaxReleasePages = 20U;

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
    const int nSha256 =
        snprintf(out->sha256Url, sizeof(out->sha256Url), "%s%s/firmware.sha256",
                 kGithubDownloadBase, tag);
    return nBin > 0 && static_cast<size_t>(nBin) < sizeof(out->binUrl) && nSha256 > 0
           && static_cast<size_t>(nSha256) < sizeof(out->sha256Url)
           && otaReleaseDownloadUrlAllowed(out->binUrl, OtaDownloadAsset::Firmware)
           && otaReleaseDownloadUrlAllowed(out->sha256Url, OtaDownloadAsset::Sha256);
}

bool httpGetGithubJson(const char* url, size_t* outLen, bool allowTruncated = false,
                       bool* outHasNext = nullptr) {
    if (url == nullptr || outLen == nullptr) {
        return false;
    }
    *outLen = 0;
    if (outHasNext != nullptr) {
        *outHasNext = false;
    }
    s_githubJsonBuf[0] = '\0';

    if (!wlanStaConnectedOk()) {
        ESP_LOGW(TAG, "GitHub update: STA Wi-Fi not connected");
        return false;
    }
    if (!chayaTlsEnsureCaBundleInstalled()) {
        ESP_LOGE(TAG, "GitHub API: CA bundle install failed");
        return false;
    }
    const unsigned long requestStartedMs = millis();
    ESP_LOGI(TAG, "GitHub API request started");

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
    const char* kCollectedHeaders[] = {"Link"};
    https.collectHeaders(kCollectedHeaders, 1);

    const int httpCode = https.GET();
    chayaTaskWatchdogReset();
    if (httpCode != HTTP_CODE_OK) {
        ESP_LOGE(TAG, "GitHub API: HTTP error %d", httpCode);
        https.end();
        return false;
    }
    if (outHasNext != nullptr) {
        *outHasNext = https.header("Link").indexOf(F("rel=\"next\"")) >= 0;
    }
    const int contentLen = https.getSize();
    if (!allowTruncated && contentLen >= static_cast<int>(kGithubJsonBuf)) {
        ESP_LOGE(TAG, "GitHub API: response too large (%d bytes)", contentLen);
        https.end();
        return false;
    }

    auto&               stream        = https.getStream();
    size_t              len           = 0;
    const unsigned long streamStartMs = millis();
    constexpr unsigned long kGithubStreamDeadlineMs = 45000UL;

    while (https.connected() && len + 1 < kGithubJsonBuf
           && (contentLen <= 0 || len < static_cast<size_t>(contentLen))
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
    const bool incompleteKnownLength =
        contentLen > 0 && len != static_cast<size_t>(contentLen);
    const bool filledBuffer = len + 1U >= kGithubJsonBuf;
    if (!allowTruncated && (incompleteKnownLength || filledBuffer)) {
        ESP_LOGE(TAG, "GitHub API: incomplete response (%u/%d bytes)",
                 static_cast<unsigned>(len), contentLen);
        return false;
    }
    ESP_LOGI(TAG, "GitHub API response received (%u bytes, %lu ms)",
             static_cast<unsigned>(len), millis() - requestStartedMs);
    return len > 0U;
}

bool releaseJsonHasRequiredAssets(const char* json) {
    return otaJsonHasAssetName(json, "firmware.bin")
           && otaJsonHasAssetName(json, "firmware.sha256");
}

bool validateReleaseAssetsByTag(const char* expectedTag) {
    if (expectedTag == nullptr || expectedTag[0] == '\0') {
        return false;
    }
    char url[256]{};
    const int n = snprintf(url, sizeof(url), "%s%s", kGithubReleaseByTagApiBase, expectedTag);
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(url)) {
        return false;
    }
    size_t len = 0;
    if (!httpGetGithubJson(url, &len)) {
        return false;
    }
    char actualTag[64]{};
    bool draft = false;
    if (!otaParseJsonStringField(s_githubJsonBuf, "tag_name", actualTag, sizeof(actualTag))
        || strcmp(actualTag, expectedTag) != 0
        || !otaParseJsonBoolField(s_githubJsonBuf, "draft", &draft) || draft) {
        ESP_LOGE(TAG, "GitHub release metadata invalid for tag %s", expectedTag);
        return false;
    }
    if (!releaseJsonHasRequiredAssets(s_githubJsonBuf)) {
        ESP_LOGE(TAG, "GitHub release %s lacks firmware.bin or firmware.sha256", expectedTag);
        return false;
    }
    return true;
}

bool evaluateTag(const char* tag, bool isPrerelease, OtaChannel channel, OtaReleaseInfo* out,
                 GithubCheckResult* result) {
    if (tag == nullptr || out == nullptr || result == nullptr) {
        return false;
    }
    if (!otaReleaseTagIsAllowed(tag) || otaVersionIsRc(tag) != isPrerelease) {
        ESP_LOGE(TAG, "GitHub release tag or prerelease flag invalid: %s", tag);
        *result = GithubCheckResult::ApiError;
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
        bool prerelease = false;
        if (!otaParseJsonBoolField(s_githubJsonBuf, "draft", &draft)
            || !otaParseJsonBoolField(s_githubJsonBuf, "prerelease", &prerelease)) {
            ESP_LOGE(TAG, "GitHub latest lacks release flags");
            return GithubCheckResult::ApiError;
        }
        if (draft || prerelease) {
            ESP_LOGW(TAG, "GitHub latest is draft/prerelease — skipping");
            return GithubCheckResult::ParsedNoUpgrade;
        }
        if (!releaseJsonHasRequiredAssets(s_githubJsonBuf)) {
            ESP_LOGE(TAG, "GitHub latest lacks firmware.bin or firmware.sha256");
            return GithubCheckResult::ApiError;
        }
        GithubCheckResult result = GithubCheckResult::ApiError;
        if (!evaluateTag(tag, false, channel, out, &result)) {
            return GithubCheckResult::ApiError;
        }
        return result;
    }

    // Beta: scan bounded one-release pages so large release notes cannot hide metadata.
    char bestPrerelease[64]{};
    char bestStable[64]{};
    bool hasNext = true;
    for (unsigned page = 1U; page <= kGithubMaxReleasePages && hasNext; ++page) {
        char url[192]{};
        const int n = snprintf(url, sizeof(url), "%s%u", kGithubReleasesListApiUrl, page);
        if (n <= 0 || static_cast<size_t>(n) >= sizeof(url)
            || !httpGetGithubJson(url, &len, true, &hasNext)) {
            return GithubCheckResult::ApiError;
        }
        char pageTag[64]{};
        bool pageIsPre = false;
        if (!otaSelectReleaseFromListJson(s_githubJsonBuf, true, pageTag, sizeof(pageTag),
                                          &pageIsPre)) {
            continue;
        }
        char* best = pageIsPre ? bestPrerelease : bestStable;
        if (best[0] == '\0' || otaVersionIsNewer(pageTag + 1, best + 1)) {
            strlcpy(best, pageTag, 64U);
        }
    }
    if (hasNext) {
        ESP_LOGW(TAG, "GitHub beta scan stopped at safety limit (%u releases)",
                 kGithubMaxReleasePages);
    }
    const bool isPre = bestPrerelease[0] != '\0';
    const char* tag  = isPre ? bestPrerelease : bestStable;
    if (tag[0] == '\0') {
        ESP_LOGE(TAG, "GitHub: no suitable beta/stable release in list");
        return GithubCheckResult::ApiError;
    }
    if (!validateReleaseAssetsByTag(tag)) {
        return GithubCheckResult::ApiError;
    }
    GithubCheckResult result = GithubCheckResult::ApiError;
    if (!evaluateTag(tag, isPre, channel, out, &result)) {
        return GithubCheckResult::ApiError;
    }
    return result;
}
