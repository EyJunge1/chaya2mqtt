#include "flash.h"

#include "ota.h"
#include "tls/tls_bundle.h"
#include "tls/tls_bundle_setup.h"

#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
#include <Arduino.h>

#include <cctype>
#include <cstdio>
#include <cstring>

#include "diag/task_watchdog.h"
#include "util/log_tag.h"

DEFINE_LOG_TAG("OTA");

namespace {

constexpr unsigned long kMd5FetchDeadlineMs = 45000UL;
constexpr int           kHttpClientTimeoutMs = 30000;

bool otaBinUrlToMd5Url(const char* binUrl, char* out, size_t outLen) {
    const size_t L = std::strlen(binUrl);
    if (L < 5 || std::strcmp(binUrl + L - 4, ".bin") != 0) {
        return false;
    }
    if (L - 4U + 4U + 1U > outLen) {
        return false;
    }
    std::memcpy(out, binUrl, L - 4U);
    std::memcpy(out + L - 4U, ".md5", 5);
    return true;
}

bool parseHexMd5(const char* hexAscii, char* outHex32, size_t outLen) {
    if (hexAscii == nullptr || outHex32 == nullptr || outLen < 33U) {
        return false;
    }
    const char* p = hexAscii;
    while (*p != '\0' && std::isxdigit(static_cast<unsigned char>(*p)) == 0) {
        ++p;
    }
    for (size_t i = 0; i < 32; ++i) {
        if (std::isxdigit(static_cast<unsigned char>(p[i])) == 0) {
            return false;
        }
        const char c = p[i];
        outHex32[i]  = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    outHex32[32] = '\0';
    return true;
}

bool httpFetchMd5Hex(WiFiClientSecure& tls, const char* md5Url, char* hexOut, size_t hexOutLen) {
    if (md5Url == nullptr || hexOut == nullptr || hexOutLen < 33) {
        return false;
    }
    HTTPClient https;
    if (!https.begin(tls, md5Url)) {
        ESP_LOGE(TAG, "MD5 URL: HTTPS begin failed");
        return false;
    }
    https.setConnectTimeout(15000);
    https.setTimeout(30000);
    https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    const int code = https.GET();
    chayaTaskWatchdogReset();
    if (code != HTTP_CODE_OK) {
        ESP_LOGE(TAG, "MD5 download HTTP %d", code);
        https.end();
        return false;
    }
    Stream&             stream       = https.getStream();
    size_t              len          = 0;
    char                raw[96]{};
    const unsigned long fetchStartMs = millis();
    while (https.connected() && len + 1 < sizeof(raw)
           && (millis() - fetchStartMs) < kMd5FetchDeadlineMs) {
        chayaTaskWatchdogReset();
        if (stream.available() <= 0) {
            if (!https.connected()) {
                break;
            }
            delay(1);
            continue;
        }
        const int c = stream.read();
        if (c < 0) {
            break;
        }
        raw[len++] = static_cast<char>(c);
    }
    raw[len] = '\0';
    https.end();
    return parseHexMd5(raw, hexOut, hexOutLen);
}

} // namespace

bool otaFlashVerifiedInstall(const char* binUrl, const char* md5Url) {
    if (binUrl == nullptr || binUrl[0] == '\0') {
        return false;
    }

    char md5UrlBuf[256];
    const char* effectiveMd5Url = md5Url;
    if (effectiveMd5Url == nullptr || effectiveMd5Url[0] == '\0') {
        if (!otaBinUrlToMd5Url(binUrl, md5UrlBuf, sizeof(md5UrlBuf))) {
            ESP_LOGE(TAG, "Cannot derive .md5 URL from firmware URL");
            return false;
        }
        effectiveMd5Url = md5UrlBuf;
    }

    if (!chayaTlsEnsureCaBundleInstalled()) {
        ESP_LOGE(TAG, "OTA: CA bundle install failed");
        return false;
    }

    WiFiClientSecure tls;
    tls.setCACertBundle(x509_crt_bundle_start,
                        static_cast<size_t>(x509_crt_bundle_end - x509_crt_bundle_start));
    tls.setTimeout(30000);

    char md5Hex[33]{};
    if (!httpFetchMd5Hex(tls, effectiveMd5Url, md5Hex, sizeof(md5Hex))) {
        ESP_LOGE(TAG, "Failed to download MD5 sidecar");
        return false;
    }

    HTTPUpdate updater(kHttpClientTimeoutMs);
    updater.rebootOnUpdate(false);
    updater.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    updater.setMD5sum(String(md5Hex));
    updater.onStart([]() {
        otaNotifyFlashProgress(0, 0);
        chayaTaskWatchdogReset();
    });
    updater.onProgress([](int current, int total) {
        otaNotifyFlashProgress(static_cast<uint32_t>(current < 0 ? 0 : current),
                               static_cast<uint32_t>(total < 0 ? 0 : total));
        chayaTaskWatchdogReset();
    });
    updater.onEnd([]() {
        otaNotifyFlashVerifying();
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
    ESP_LOGI(TAG, "HTTPUpdate OK (MD5 verified)");
    return true;
}
