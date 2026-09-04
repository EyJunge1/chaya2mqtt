#include "github.h"

#include "github_parse.h"
#include "ota_url_allow.h"
#include "version_cmp.h"

#include "config/version.h"
#include "tls/tls_bundle_setup.h"
#include "wifi/wlan.h"

#include "tls/tls_bundle.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <cstdio>
#include <cstring>
#include <esp_log.h>

#include "diag/task_watchdog.h"
#include "util/log_tag.h"

DEFINE_LOG_TAG("OTA");

namespace {

constexpr const char kGithubLatestReleaseApiUrl[] = "https://api.github.com/repos/EyJunge1/chaya2mqtt/releases/latest";

constexpr const char kGithubReleasesListApiUrlFmt[] = "https://api.github.com/repos/EyJunge1/chaya2mqtt/releases?per_page=%u";

constexpr const char kGithubDownloadBase[] = "https://github.com/EyJunge1/chaya2mqtt/releases/download/";

constexpr unsigned kGithubReleasesPerPage = 20U;
constexpr unsigned long kGithubStreamDeadlineMs = 45000UL;

class GithubApiStream final : public Stream {
  public:
    GithubApiStream(NetworkClient &inner, unsigned long deadlineMs)
        : inner_(inner), deadlineMs_(deadlineMs), startMs_(millis()) {}

    int available() override { return timedOut() ? 0 : inner_.available(); }

    int read() override {
        char c = 0;
        if (readBytes(&c, 1) != 1U) {
            return -1;
        }
        return static_cast<int>(static_cast<unsigned char>(c));
    }

    int peek() override {
        if (timedOut()) {
            return -1;
        }
        return inner_.peek();
    }

    size_t readBytes(char *buffer, size_t length) override {
        if (buffer == nullptr || length == 0U) {
            return 0;
        }
        size_t got = 0;
        while (got < length) {
            chayaTaskWatchdogReset();
            if (timedOut()) {
                break;
            }
            const int n = inner_.read(reinterpret_cast<uint8_t *>(buffer + got), length - got);
            if (n < 0) {
                break;
            }
            if (n > 0) {
                got += static_cast<size_t>(n);
                continue;
            }
            if (!inner_.connected()) {
                break;
            }
            delay(2);
        }
        return got;
    }

    void flush() override { inner_.flush(); }

    size_t write(uint8_t) override { return 0; }

    bool timedOut() const { return (millis() - startMs_) >= deadlineMs_; }

  private:
    NetworkClient &inner_;
    unsigned long deadlineMs_;
    unsigned long startMs_;
};

void stripLeadingV(const char *tag, char *out, size_t outLen) {
    if (out == nullptr || outLen == 0U) {
        return;
    }
    out[0] = '\0';
    if (tag == nullptr || tag[0] == '\0') {
        return;
    }
    const char *p = tag;
    if (p[0] == 'v' || p[0] == 'V') {
        ++p;
    }
    strlcpy(out, p, outLen);
}

bool fillReleaseUrls(const char *tag, OtaReleaseInfo *out) {
    if (tag == nullptr || tag[0] == '\0' || out == nullptr) {
        return false;
    }
    strlcpy(out->tag, tag, sizeof(out->tag));
    stripLeadingV(tag, out->version, sizeof(out->version));
    const int nBin = snprintf(out->binUrl, sizeof(out->binUrl), "%s%s/firmware.bin", kGithubDownloadBase, tag);
    const int nSha256 = snprintf(out->sha256Url, sizeof(out->sha256Url), "%s%s/firmware.sha256", kGithubDownloadBase, tag);
    return nBin > 0 && static_cast<size_t>(nBin) < sizeof(out->binUrl) && nSha256 > 0 &&
           static_cast<size_t>(nSha256) < sizeof(out->sha256Url) &&
           otaReleaseDownloadUrlAllowed(out->binUrl, OtaDownloadAsset::Firmware) &&
           otaReleaseDownloadUrlAllowed(out->sha256Url, OtaDownloadAsset::Sha256);
}

bool httpGetGithubJson(const char *url, JsonDocument &doc, bool list, bool *outHasNext = nullptr) {
    if (url == nullptr) {
        return false;
    }
    if (outHasNext != nullptr) {
        *outHasNext = false;
    }

    if (!wlanStaConnectedOk()) {
        ESP_LOGW(TAG, "GitHub update: STA Wi-Fi not connected");
        return false;
    }
    if (!chayaTlsEnsureCaBundleInstalled()) {
        ESP_LOGE(TAG, "GitHub API: CA bundle install failed");
        return false;
    }
#if defined(CORE_DEBUG_LEVEL) && CORE_DEBUG_LEVEL >= 3
    const unsigned long requestStartedMs = millis();
#endif
    ESP_LOGI(TAG, "GitHub API request started");

    WiFiClientSecure tls;
    tls.setCACertBundle(x509_crt_bundle_start, static_cast<size_t>(x509_crt_bundle_end - x509_crt_bundle_start));
    tls.setTimeout(30000);

    HTTPClient https;
    // HTTP/1.0 disables chunked transfer so ArduinoJson can read getStream() directly.
    https.useHTTP10(true);
    if (!https.begin(tls, url)) {
        ESP_LOGE(TAG, "GitHub API: HTTPS begin failed");
        return false;
    }
    https.setConnectTimeout(15000);
    https.setTimeout(30000);
    https.addHeader(F("User-Agent"), F("Chaya2MQTT-esp32"));
    https.addHeader(F("Accept"), F("application/vnd.github+json"));
    const char *kCollectedHeaders[] = {"Link"};
    if (outHasNext != nullptr) {
        https.collectHeaders(kCollectedHeaders, 1);
    }

    struct HttpEnd {
        HTTPClient &http;
        ~HttpEnd() { http.end(); }
    } closer{https};

    const int httpCode = https.GET();
    chayaTaskWatchdogReset();
    if (httpCode != HTTP_CODE_OK) {
        ESP_LOGE(TAG, "GitHub API: HTTP error %d", httpCode);
        return false;
    }
    if (outHasNext != nullptr) {
        *outHasNext = https.header("Link").indexOf(F("rel=\"next\"")) >= 0;
    }

    NetworkClient *client = https.getStreamPtr();
    if (client == nullptr) {
        ESP_LOGE(TAG, "GitHub API: no response stream");
        return false;
    }
    GithubApiStream stream(*client, kGithubStreamDeadlineMs);
    if (!otaDeserializeGithubReleaseJson(stream, doc, list)) {
        ESP_LOGE(TAG, "GitHub API: %s", stream.timedOut() ? "parse timeout" : "failed to parse JSON");
        return false;
    }
    ESP_LOGI(TAG, "GitHub API response parsed (%d bytes, %lu ms)", https.getSize(), millis() - requestStartedMs);
    return true;
}

GithubCheckResult evaluateTag(const char *tag, bool isPrerelease, OtaChannel channel, OtaReleaseInfo *out) {
    if (tag == nullptr || out == nullptr) {
        return GithubCheckResult::ApiError;
    }
    if (!otaReleaseTagIsAllowed(tag) || otaVersionIsRc(tag) != isPrerelease) {
        ESP_LOGE(TAG, "GitHub release tag or prerelease flag invalid: %s", tag);
        return GithubCheckResult::ApiError;
    }
    if (!fillReleaseUrls(tag, out)) {
        return GithubCheckResult::ApiError;
    }
    out->channel = channel;
    out->isPrerelease = isPrerelease;

    ESP_LOGI(TAG, "GitHub channel=%s tag=%s local=%s", channel == OtaChannel::Beta ? "beta" : "stable", tag, APP_VERSION);

    if (!otaVersionIsNewer(tag, APP_VERSION)) {
        return GithubCheckResult::ParsedNoUpgrade;
    }
    return GithubCheckResult::ParsedUpgradeAvail;
}

} // namespace

GithubCheckResult otaGithubEvaluateChannel(OtaChannel channel, OtaReleaseInfo *out) {
    if (out == nullptr) {
        return GithubCheckResult::ApiError;
    }
    *out = OtaReleaseInfo{};
    out->channel = channel;

    if (channel == OtaChannel::Stable) {
        JsonDocument doc;
        if (!httpGetGithubJson(kGithubLatestReleaseApiUrl, doc, false)) {
            return GithubCheckResult::ApiError;
        }
        const JsonVariantConst root = doc.as<JsonVariantConst>();
        char tag[64]{};
        if (!otaCopyJsonString(doc["tag_name"], tag, sizeof(tag))) {
            ESP_LOGE(TAG, "GitHub: failed to parse tag_name");
            return GithubCheckResult::ApiError;
        }
        bool draft = false;
        bool prerelease = false;
        if (!otaParseJsonBoolField(root, "draft", &draft) || !otaParseJsonBoolField(root, "prerelease", &prerelease)) {
            ESP_LOGE(TAG, "GitHub latest lacks release flags");
            return GithubCheckResult::ApiError;
        }
        if (draft || prerelease) {
            ESP_LOGW(TAG, "GitHub latest is draft/prerelease — skipping");
            return GithubCheckResult::ParsedNoUpgrade;
        }
        if (!otaReleaseHasRequiredAssets(root)) {
            ESP_LOGE(TAG, "GitHub latest lacks firmware.bin or firmware.sha256");
            return GithubCheckResult::ApiError;
        }
        return evaluateTag(tag, false, channel, out);
    }

    char listUrl[192]{};
    const int n = snprintf(listUrl, sizeof(listUrl), kGithubReleasesListApiUrlFmt, kGithubReleasesPerPage);
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(listUrl)) {
        return GithubCheckResult::ApiError;
    }
    JsonDocument doc;
    bool hasNext = false;
    if (!httpGetGithubJson(listUrl, doc, true, &hasNext)) {
        return GithubCheckResult::ApiError;
    }
    if (hasNext) {
        ESP_LOGW(TAG, "GitHub beta scan limited to %u releases", kGithubReleasesPerPage);
    }
    char tag[64]{};
    bool isPre = false;
    if (!otaSelectReleaseFromListJson(doc.as<JsonVariantConst>(), true, tag, sizeof(tag), &isPre, true)) {
        ESP_LOGE(TAG, "GitHub: no suitable beta/stable release in list");
        return GithubCheckResult::ApiError;
    }
    return evaluateTag(tag, isPre, channel, out);
}
