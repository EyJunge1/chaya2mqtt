#include <Arduino.h>

#include "ota.h"

#include "constants.h"
#include "counter.h"
#include "tls_bundle.h"
#include "version.h"
#include "wlan.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <esp_log.h>

#if defined(CORE_DEBUG_LEVEL) && CORE_DEBUG_LEVEL > 0
static const char* TAG = "OTA";
#else
static constexpr const char* TAG __attribute__((unused)) = "";
#endif

static constexpr const char kGithubLatestReleaseApiUrl[] =
    "https://api.github.com/repos/EyJunge1/chaya2mqtt/releases/latest";

static constexpr const char kGithubLatestFirmwareBinUrl[] =
    "https://github.com/EyJunge1/chaya2mqtt/releases/latest/download/firmware.bin";

static constexpr const char kCfgNamespace[]           = "cfg";
static constexpr const char kNvKeyUpdateCalendarDay[] = "upd_day";

static constexpr size_t kOtaUrlMax = 256;

static std::atomic<bool> g_otaRequested{false};
static std::atomic<bool> g_otaCheckRequested{false};

static char g_otaUrl[kOtaUrlMax];

static uint32_t s_cachedNvUpdateDay     = UINT32_MAX;
static bool     s_nvUpdateDayCacheValid = false;

/** Large JSON accumulator — static to avoid multi-KiB stack usage under TLS. */
static constexpr size_t kGithubJsonBuf = 8192;
static char             s_githubJsonBuf[kGithubJsonBuf];

static bool githubParseLatestTagLegacy(const char* json, char* tagOut, size_t tagLen) {
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

static bool githubExtractTagFromJsonBuffer(char* jsonBuf, size_t len, char* tagOut, size_t tagLen) {
    if (len >= kGithubJsonBuf) {
        len = kGithubJsonBuf - 1;
    }
    jsonBuf[len] = '\0';

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, jsonBuf);
    if (!err) {
        const char* tag = doc["tag_name"].as<const char*>();
        if (tag != nullptr && tag[0] != '\0') {
            strlcpy(tagOut, tag, tagLen);
            return true;
        }
    }
    return githubParseLatestTagLegacy(jsonBuf, tagOut, tagLen);
}

static bool nvLoadLastUpdateCalendarDay(uint32_t* outDayUtc) {
    if (outDayUtc == nullptr) {
        return false;
    }
    Preferences prefs;
    if (!prefs.begin(kCfgNamespace, true)) {
        *outDayUtc = 0;
        return false;
    }
    *outDayUtc = prefs.getUInt(kNvKeyUpdateCalendarDay, 0);
    prefs.end();
    return true;
}

static void nvSaveLastUpdateCalendarDay(uint32_t dayUtc) {
    Preferences prefs;
    if (!prefs.begin(kCfgNamespace, false)) {
        ESP_LOGE(TAG, "NVS cfg: kann upd_day nicht schreiben");
        return;
    }
    prefs.putUInt(kNvKeyUpdateCalendarDay, dayUtc);
    prefs.end();
    s_cachedNvUpdateDay     = dayUtc;
    s_nvUpdateDayCacheValid = true;
}

/** @return true if GitHub API responded OK and tag_name was parsed successfully */
static bool checkGithubUpdate() {
    if (!wlanStaConnectedOk()) {
        ESP_LOGW(TAG, "GitHub Update: kein Stations-WLAN");
        return false;
    }

    WiFiClientSecure tls;
    tls.setCACertBundle(x509_crt_bundle_start,
                        static_cast<size_t>(x509_crt_bundle_end - x509_crt_bundle_start));
    tls.setTimeout(45000);

    HTTPClient https;
    if (!https.begin(tls, kGithubLatestReleaseApiUrl)) {
        ESP_LOGE(TAG, "GitHub API: HTTPS begin fehlgeschlagen");
        return false;
    }

    https.setConnectTimeout(20000);
    https.setTimeout(45000);
    https.addHeader(F("User-Agent"), F("Chaya2MQTT-esp32"));
    https.addHeader(F("Accept"), F("application/vnd.github+json"));

    const int httpCode = https.GET();
    if (httpCode != HTTP_CODE_OK) {
        ESP_LOGE(TAG, "GitHub API: HTTP-Fehler %d", httpCode);
        https.end();
        return false;
    }

    auto&               stream          = https.getStream();
    char                remoteTag[64];
    size_t              len             = 0;
    const unsigned long streamStartMs = millis();

    while (https.connected() && len + 1 < kGithubJsonBuf && (millis() - streamStartMs) < 45000UL) {
        if (stream.available() <= 0) {
            if (!https.connected()) {
                break;
            }
            delay(10);
            continue;
        }
        const int toRead = std::min(static_cast<int>(kGithubJsonBuf - 1 - len), stream.available());
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

    const bool parseOk =
        (len > 0)
        && githubExtractTagFromJsonBuffer(s_githubJsonBuf, len, remoteTag, sizeof(remoteTag));

    if (!parseOk) {
        ESP_LOGE(TAG, "GitHub: konnte tag_name nicht parsen");
        return false;
    }

    ESP_LOGI(TAG, "GitHub latest=%s, lokal=%s", remoteTag, APP_VERSION);

    if (strcmp(remoteTag, APP_VERSION) != 0) {
        ESP_LOGI(TAG, "Firmware-Update: neue Version auf GitHub verfügbar");
        strlcpy(g_otaUrl, kGithubLatestFirmwareBinUrl, sizeof(g_otaUrl));
        g_otaRequested.store(true, std::memory_order_release);
    } else {
        ESP_LOGI(TAG, "Firmware ist aktuell");
    }
    return true;
}

static void autoUpdateLoop() {
    if (configIsApMode()) {
        return;
    }
    const bool wlanStaOk = wlanStaConnectedOk();
    if (!wlanStaOk) {
        return;
    }

    const time_t   utcNow      = time(nullptr);
    const uint32_t todayUtcDay = calendarDaySinceEpochUtc(utcNow > 0 ? utcNow : 0);

    if (g_otaCheckRequested.exchange(false, std::memory_order_acq_rel)) {
        const bool ntpOk = ntpTimeLooksSynced(utcNow);
        if (!ntpOk) {
            ESP_LOGW(TAG, "Manual update check: Zeit noch nicht plausibel (NTP?), prüfe trotzdem GitHub …");
        }
        const bool checkOk = checkGithubUpdate();
        if (ntpOk && checkOk) {
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
        (void)nvLoadLastUpdateCalendarDay(&s_cachedNvUpdateDay);
        s_nvUpdateDayCacheValid = true;
    }
    if (s_cachedNvUpdateDay == todayUtcDay) {
        return;
    }

    if (checkGithubUpdate()) {
        nvSaveLastUpdateCalendarDay(todayUtcDay);
    }
}

bool otaQueueFirmwareUrl(const char* url) {
    if (url == nullptr || std::strlen(url) < 10 || std::strlen(url) >= sizeof(g_otaUrl)) {
        return false;
    }
    strlcpy(g_otaUrl, url, sizeof(g_otaUrl));
    g_otaRequested.store(true, std::memory_order_release);
    return true;
}

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

        WiFiClientSecure client;
        client.setCACertBundle(x509_crt_bundle_start,
                               static_cast<size_t>(x509_crt_bundle_end - x509_crt_bundle_start));
        HTTPUpdate httpUpdate;
        httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
        const t_httpUpdate_return rc = httpUpdate.update(client, urlCopy);
        if (rc == HTTP_UPDATE_OK) {
            ESP_LOGI(TAG, "OTA ok, Neustart");
            flushHeartCounterIfDirty();
            flushHeartSentCounterIfDirty();
            delay(200);
            releaseGpioHoldBeforeRestart();
            ESP.restart();
        } else if (rc == HTTP_UPDATE_NO_UPDATES) {
            ESP_LOGW(TAG, "OTA: keine Updates");
        } else {
            ESP_LOGE(TAG, "OTA Fehler=%d (%s)",
                     static_cast<int>(httpUpdate.getLastError()), httpUpdate.getLastErrorString().c_str());
        }
    }
}
