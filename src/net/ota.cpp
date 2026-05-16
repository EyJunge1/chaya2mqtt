#include <WiFi.h>

#include <Arduino.h>

#include "ota.h"

#include "constants.h"
#include "counter.h"
#include "tls_bundle.h"
#include "version.h"
#include "wlan.h"
#include "nvs_utils.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <algorithm>
#include <atomic>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <mbedtls/sha256.h>

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

/** GitHub release JSON max read size (heap-allocated only inside checkGithubUpdate). */
static constexpr size_t kGithubJsonBuf = 8192;

/** OTA streaming read buffer (TLS); static to keep stack small in httpStreamFirmwareToOtaVerified. */
static constexpr size_t                    kOtaChunkSize = 4096;
static std::array<uint8_t, kOtaChunkSize> s_otaFwReadChunk{};

static uint32_t semverPackFromTag(const char* tag) {
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
    const int    n = sscanf(p, "%u.%u.%u", &major, &minor, &patch);
    if (n < 1) {
        return 0;
    }
    if (major > 999U || minor > 999U || patch > 999U) {
        return UINT32_MAX;
    }
    return major * 1000000U + minor * 1000U + patch;
}

/** Replace trailing ".bin" with ".sha256" (same path, same host). */
static bool otaBinUrlToSha256Url(const char* binUrl, char* out, size_t outLen) {
    const size_t L = std::strlen(binUrl);
    if (L < 5 || std::strcmp(binUrl + L - 4, ".bin") != 0) {
        return false;
    }
    /* ".bin"(4) -> ".sha256"(7) => +3 bytes before NUL */
    if (L - 4U + 7U + 1U > outLen) {
        return false;
    }
    std::memcpy(out, binUrl, L - 4U);
    std::memcpy(out + L - 4U, ".sha256", 8);
    return true;
}

static bool parseHexSha256(const char* hexAscii, std::array<uint8_t, 32>& out) {
    if (hexAscii == nullptr) {
        return false;
    }
    const char* p = hexAscii;
    while (*p != '\0' && std::isxdigit(static_cast<unsigned char>(*p)) == 0) {
        ++p;
    }
    for (size_t i = 0; i < 64; ++i) {
        if (std::isxdigit(static_cast<unsigned char>(p[i])) == 0) {
            return false;
        }
    }
    for (size_t i = 0; i < 32; ++i) {
        char buf[3];
        buf[0] = p[i * 2];
        buf[1] = p[i * 2 + 1];
        buf[2] = '\0';
        out[i] = static_cast<uint8_t>(strtoul(buf, nullptr, 16));
    }
    return true;
}

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

static void nvLoadLastUpdateCalendarDay(uint32_t* outDayUtc) {
    if (outDayUtc == nullptr) {
        return;
    }
    *outDayUtc = app_nvs::readUInt(kCfgNamespace, kNvKeyUpdateCalendarDay, 0);
}

static void nvSaveLastUpdateCalendarDay(uint32_t dayUtc) {
    if (!app_nvs::writeUInt(kCfgNamespace, kNvKeyUpdateCalendarDay, dayUtc)) {
        ESP_LOGE(TAG, "NVS cfg: kann upd_day nicht schreiben");
        return;
    }
    s_cachedNvUpdateDay     = dayUtc;
    s_nvUpdateDayCacheValid = true;
}

static bool httpFetchSha256Hex(WiFiClientSecure& tls, const char* shaUrl, char* hexOut,
                               size_t hexOutLen) {
    if (shaUrl == nullptr || hexOut == nullptr || hexOutLen < 65) {
        return false;
    }
    HTTPClient https;
    if (!https.begin(tls, shaUrl)) {
        ESP_LOGE(TAG, "SHA URL begin fehlgeschlagen");
        return false;
    }
    https.setConnectTimeout(20000);
    https.setTimeout(45000);
    https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    const int code = https.GET();
    if (code != HTTP_CODE_OK) {
        ESP_LOGE(TAG, "SHA256 Download HTTP %d", code);
        https.end();
        return false;
    }
    WiFiClient& stream = https.getStream();
    size_t      len    = 0;
    while (https.connected() && len + 1 < hexOutLen) {
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
        hexOut[len++] = static_cast<char>(c);
    }
    hexOut[len] = '\0';
    https.end();
    return len >= 64;
}

static bool httpStreamFirmwareToOtaVerified(WiFiClientSecure& tls, const char* binUrl,
                                            const std::array<uint8_t, 32>& expectedHash) {
    HTTPClient https;
    if (!https.begin(tls, binUrl)) {
        ESP_LOGE(TAG, "Firmware URL begin fehlgeschlagen");
        return false;
    }
    https.setConnectTimeout(20000);
    https.setTimeout(60000);
    https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    const int httpCode = https.GET();
    if (httpCode != HTTP_CODE_OK) {
        ESP_LOGE(TAG, "Firmware Download HTTP %d", httpCode);
        https.end();
        return false;
    }

    const esp_partition_t* updatePart = esp_ota_get_next_update_partition(nullptr);
    if (updatePart == nullptr) {
        ESP_LOGE(TAG, "Keine OTA-Partition");
        https.end();
        return false;
    }

    esp_ota_handle_t otaHandle = 0;
    esp_err_t        err =
        esp_ota_begin(updatePart, OTA_WITH_SEQUENTIAL_WRITES, &otaHandle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin: %s", esp_err_to_name(err));
        https.end();
        return false;
    }

    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);

    WiFiClient& stream       = https.getStream();
    size_t      totalWritten = 0;

    const int           contentLen = https.getSize();
    const unsigned long startMs    = millis();

    if (contentLen > 0) {
        int remain = contentLen;
        while (remain > 0) {
            if (millis() - startMs > 300000UL) {
                ESP_LOGE(TAG, "Firmware-Download Timeout");
                esp_ota_abort(otaHandle);
                mbedtls_sha256_free(&sha);
                https.end();
                return false;
            }
            if (stream.available() <= 0) {
                if (!https.connected()) {
                    break;
                }
                delay(1);
                continue;
            }
            const int take = std::min(remain, static_cast<int>(s_otaFwReadChunk.size()));
            const size_t n = stream.readBytes(s_otaFwReadChunk.data(), static_cast<size_t>(take));
            if (n == 0) {
                break;
            }
            mbedtls_sha256_update(&sha, s_otaFwReadChunk.data(), n);
            err = esp_ota_write(otaHandle, s_otaFwReadChunk.data(), n);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "esp_ota_write: %s", esp_err_to_name(err));
                esp_ota_abort(otaHandle);
                mbedtls_sha256_free(&sha);
                https.end();
                return false;
            }
            totalWritten += n;
            remain -= static_cast<int>(n);
        }
        if (remain != 0) {
            ESP_LOGE(TAG, "Firmware unvollstaendig (%d Bytes fehlen)", remain);
            esp_ota_abort(otaHandle);
            mbedtls_sha256_free(&sha);
            https.end();
            return false;
        }
    } else {
        while (https.connected() || stream.available() > 0) {
            if (millis() - startMs > 300000UL) {
                ESP_LOGE(TAG, "Firmware-Download Timeout");
                esp_ota_abort(otaHandle);
                mbedtls_sha256_free(&sha);
                https.end();
                return false;
            }
            if (stream.available() <= 0) {
                if (!https.connected()) {
                    break;
                }
                delay(1);
                continue;
            }
            const size_t n = stream.readBytes(s_otaFwReadChunk.data(), s_otaFwReadChunk.size());
            if (n == 0) {
                break;
            }
            mbedtls_sha256_update(&sha, s_otaFwReadChunk.data(), n);
            err = esp_ota_write(otaHandle, s_otaFwReadChunk.data(), n);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "esp_ota_write: %s", esp_err_to_name(err));
                esp_ota_abort(otaHandle);
                mbedtls_sha256_free(&sha);
                https.end();
                return false;
            }
            totalWritten += n;
        }
    }
    https.end();

    std::array<uint8_t, 32> computed{};
    mbedtls_sha256_finish(&sha, computed.data());
    mbedtls_sha256_free(&sha);

    if (memcmp(computed.data(), expectedHash.data(), 32) != 0) {
        ESP_LOGE(TAG, "SHA256 mismatch (firmware nicht verifiziert), written=%u",
                 static_cast<unsigned>(totalWritten));
        esp_ota_abort(otaHandle);
        return false;
    }

    err = esp_ota_end(otaHandle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end: %s", esp_err_to_name(err));
        return false;
    }

    err = esp_ota_set_boot_partition(updatePart);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "OTA verifiziert, %u Bytes", static_cast<unsigned>(totalWritten));
    return true;
}

static bool runVerifiedOtaFromUrls(const char* binUrl) {
    char shaUrl[kOtaUrlMax];
    if (!otaBinUrlToSha256Url(binUrl, shaUrl, sizeof(shaUrl))) {
        ESP_LOGE(TAG, "Keine .bin URL fuer SHA256-Ableitung");
        return false;
    }

    WiFiClientSecure tls;
    tls.setCACertBundle(x509_crt_bundle_start,
                        static_cast<size_t>(x509_crt_bundle_end - x509_crt_bundle_start));
    tls.setTimeout(60000);

    char hexBuf[96];
    hexBuf[0] = '\0';
    if (!httpFetchSha256Hex(tls, shaUrl, hexBuf, sizeof(hexBuf))) {
        ESP_LOGE(TAG, "SHA256-Datei laden fehlgeschlagen");
        return false;
    }

    std::array<uint8_t, 32> expected{};
    if (!parseHexSha256(hexBuf, expected)) {
        ESP_LOGE(TAG, "SHA256 Parsing fehlgeschlagen");
        return false;
    }

    if (!httpStreamFirmwareToOtaVerified(tls, binUrl, expected)) {
        return false;
    }
    return true;
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

    std::unique_ptr<char[]> jsonHeap(new (std::nothrow) char[kGithubJsonBuf]);
    if (!jsonHeap) {
        ESP_LOGE(TAG, "GitHub API: JSON-Puffer allozieren fehlgeschlagen");
        https.end();
        return false;
    }
    char* const jsonBuf = jsonHeap.get();

    auto&               stream       = https.getStream();
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
        const int n = stream.readBytes(jsonBuf + len, toRead);
        if (n <= 0) {
            break;
        }
        len += static_cast<size_t>(n);
    }

    https.end();

    const bool parseOk =
        (len > 0)
        && githubExtractTagFromJsonBuffer(jsonBuf, len, remoteTag, sizeof(remoteTag));

    if (!parseOk) {
        ESP_LOGE(TAG, "GitHub: konnte tag_name nicht parsen");
        return false;
    }

    ESP_LOGI(TAG, "GitHub latest=%s, lokal=%s", remoteTag, APP_VERSION);

    const uint32_t remoteV = semverPackFromTag(remoteTag);
    const uint32_t localV  = semverPackFromTag(APP_VERSION);
    if (remoteV == UINT32_MAX || localV == UINT32_MAX) {
        ESP_LOGW(TAG, "Semver parse unsicher, kein Auto-Update");
        return true;
    }
    if (remoteV > localV) {
        ESP_LOGI(TAG, "Firmware-Update: neuere Version auf GitHub verfuegbar");
        strlcpy(g_otaUrl, kGithubLatestFirmwareBinUrl, sizeof(g_otaUrl));
        g_otaRequested.store(true, std::memory_order_release);
    } else {
        ESP_LOGI(TAG, "Firmware ist aktuell oder nicht neuer");
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

        const bool ok = runVerifiedOtaFromUrls(urlCopy);
        if (!ok) {
            ESP_LOGE(TAG, "OTA abgebrochen (ohne Neustart)");
            return;
        }

        flushHeartCounterIfDirty();
        flushHeartSentCounterIfDirty();
        delay(200);
        releaseGpioHoldBeforeRestart();
        ESP.restart();
    }
}
