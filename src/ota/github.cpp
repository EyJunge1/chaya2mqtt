#include "github.h"

#include "tls_bundle_setup.h"
#include "version.h"
#include "wifi/wlan.h"

#include "tls_bundle.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <Arduino.h>
#include <esp_log.h>

#include "diag/task_watchdog.h"
#include "log_tag.h"

DEFINE_LOG_TAG("OTA");

namespace {

constexpr const char kGithubLatestReleaseApiUrl[] =
    "https://api.github.com/repos/EyJunge1/chaya2mqtt/releases/latest";

constexpr const char kGithubLatestFirmwareBinUrl[] =
    "https://github.com/EyJunge1/chaya2mqtt/releases/latest/download/firmware.bin";

constexpr size_t kGithubJsonBuf = 8192;

char s_githubJsonBuf[kGithubJsonBuf];

uint32_t semverPackFromTag(const char* tag) {
    if (tag == nullptr || tag[0] == '\0') {
        return 0;
    }
    const char* p = tag;
    if (p[0] == 'v' || p[0] == 'V') {
        ++p;
    }
    unsigned major = 0;
    unsigned minor = 0;
    unsigned patch = 0;
    const int n = sscanf(p, "%u.%u.%u", &major, &minor, &patch);
    if (n < 1) {
        return 0;
    }
    if (major > 999U || minor > 999U || patch > 999U) {
        return UINT32_MAX;
    }
    return major * 1000000U + minor * 1000U + patch;
}

bool githubParseLatestTagLegacy(const char* json, char* tagOut, size_t tagLen) {
    constexpr const char kKey[] = "\"tag_name\"";
    const char*          key    = strstr(json, kKey);
    if (key == nullptr) {
        return false;
    }
    const char* colon = strchr(key, ':');
    if (colon == nullptr) {
        return false;
    }
    const char* p = colon + 1;
    while (*p == ' ' || *p == '\t') {
        ++p;
    }
    if (*p != '\"') {
        return false;
    }
    ++p;
    size_t i = 0;
    while (*p != '\0' && *p != '\"' && (i + 1) < tagLen) {
        tagOut[i++] = *p++;
    }
    tagOut[i] = '\0';
    return i > 0;
}

bool githubExtractTagFromJsonBuffer(char* jsonBuf, size_t len, char* tagOut, size_t tagLen) {
    if (len >= kGithubJsonBuf) {
        len = kGithubJsonBuf - 1;
    }
    jsonBuf[len] = '\0';
    return githubParseLatestTagLegacy(jsonBuf, tagOut, tagLen);
}

} // namespace

GithubCheckResult otaGithubEvaluateLatestRelease(char* firmwareUrlBuf,
                                                 size_t firmwareUrlBufLen) {
    if (firmwareUrlBuf == nullptr || firmwareUrlBufLen == 0U) {
        return GithubCheckResult::ApiError;
    }
    firmwareUrlBuf[0] = '\0';

    if (!wlanStaConnectedOk()) {
        ESP_LOGW(TAG, "GitHub update: STA Wi-Fi not connected");
        return GithubCheckResult::ApiError;
    }

    if (!chayaTlsEnsureCaBundleInstalled()) {
        ESP_LOGE(TAG, "GitHub API: CA bundle install failed");
        return GithubCheckResult::ApiError;
    }

    WiFiClientSecure tls;
    tls.setCACertBundle(x509_crt_bundle_start,
                        static_cast<size_t>(x509_crt_bundle_end - x509_crt_bundle_start));
    tls.setTimeout(45000);

    HTTPClient https;
    if (!https.begin(tls, kGithubLatestReleaseApiUrl)) {
        ESP_LOGE(TAG, "GitHub API: HTTPS begin failed");
        return GithubCheckResult::ApiError;
    }

    https.setConnectTimeout(20000);
    https.setTimeout(45000);
    https.addHeader(F("User-Agent"), F("Chaya2MQTT-esp32"));
    https.addHeader(F("Accept"), F("application/vnd.github+json"));

    const int httpCode = https.GET();
    if (httpCode != HTTP_CODE_OK) {
        ESP_LOGE(TAG, "GitHub API: HTTP error %d", httpCode);
        https.end();
        return GithubCheckResult::ApiError;
    }

    auto&               stream        = https.getStream();
    char                remoteTag[64]{};
    size_t              len           = 0;
    bool                tagParsed     = false;
    const unsigned long streamStartMs = millis();

    while (https.connected() && len + 1 < kGithubJsonBuf && (millis() - streamStartMs) < 45000UL) {
        chayaTaskWatchdogReset();
        if (tagParsed) {
            break;
        }
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
        tagParsed = githubExtractTagFromJsonBuffer(s_githubJsonBuf, len, remoteTag, sizeof(remoteTag));
    }

    https.end();

    if (!tagParsed) {
        tagParsed =
            (len > 0)
            && githubExtractTagFromJsonBuffer(s_githubJsonBuf, len, remoteTag, sizeof(remoteTag));
    }

    if (!tagParsed) {
        ESP_LOGE(TAG, "GitHub: failed to parse tag_name");
        return GithubCheckResult::ApiError;
    }

    ESP_LOGI(TAG, "GitHub latest=%s local=%s", remoteTag, APP_VERSION);

    const uint32_t remoteV = semverPackFromTag(remoteTag);
    const uint32_t localV  = semverPackFromTag(APP_VERSION);
    if (remoteV == UINT32_MAX || localV == UINT32_MAX) {
        ESP_LOGW(TAG, "GitHub semver parse uncertain — skipping auto-upgrade");
        return GithubCheckResult::ParsedNoUpgrade;
    }
    if (remoteV > localV) {
        ESP_LOGI(TAG, "Newer firmware reported on GitHub");
        static_cast<void>(strlcpy(firmwareUrlBuf, kGithubLatestFirmwareBinUrl, firmwareUrlBufLen));
        return GithubCheckResult::ParsedUpgradeAvail;
    }
    ESP_LOGI(TAG, "Firmware is up to date");
    return GithubCheckResult::ParsedNoUpgrade;
}
