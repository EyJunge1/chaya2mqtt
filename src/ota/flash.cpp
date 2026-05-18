#include "flash.h"

#include "tls_bundle.h"

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Arduino.h>

#include <array>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <esp_log.h>

#include "diag/task_watchdog.h"
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <mbedtls/sha256.h>

#include "log_tag.h"

DEFINE_LOG_TAG("OTA");

namespace {

constexpr size_t kOtaChunkSize = 4096;
std::array<uint8_t, kOtaChunkSize> g_otaFwReadChunk{};

struct MbedtlsSha256Session {
    mbedtls_sha256_context ctx{};
    bool                   active = false;

    MbedtlsSha256Session() {
        mbedtls_sha256_init(&ctx);
        mbedtls_sha256_starts(&ctx, 0);
        active = true;
    }

    void disarmNoFree() {
        active = false;
    }

    ~MbedtlsSha256Session() {
        if (active) {
            mbedtls_sha256_free(&ctx);
        }
    }

    mbedtls_sha256_context* ptr() {
        return &ctx;
    }
};

struct EspOtaSession {
    esp_ota_handle_t h = 0;

    ~EspOtaSession() {
        abandon();
    }

    void abandon() {
        if (h != 0) {
            esp_ota_abort(h);
            h = 0;
        }
    }

    void disarmCommitted() {
        h = 0;
    }

    esp_ota_handle_t* addr() {
        return &h;
    }
};

bool otaBinUrlToSha256Url(const char* binUrl, char* out, size_t outLen) {
    const size_t L = std::strlen(binUrl);
    if (L < 5 || std::strcmp(binUrl + L - 4, ".bin") != 0) {
        return false;
    }
    if (L - 4U + 7U + 1U > outLen) {
        return false;
    }
    std::memcpy(out, binUrl, L - 4U);
    std::memcpy(out + L - 4U, ".sha256", 8);
    return true;
}

bool parseHexSha256(const char* hexAscii, std::array<uint8_t, 32>& out) {
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

bool httpFetchSha256Hex(WiFiClientSecure& tls, const char* shaUrl, char* hexOut,
                        size_t hexOutLen) {
    if (shaUrl == nullptr || hexOut == nullptr || hexOutLen < 65) {
        return false;
    }
    HTTPClient https;
    if (!https.begin(tls, shaUrl)) {
        ESP_LOGE(TAG, "SHA URL: HTTPS begin failed");
        return false;
    }
    https.setConnectTimeout(20000);
    https.setTimeout(45000);
    https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    const int code = https.GET();
    if (code != HTTP_CODE_OK) {
        ESP_LOGE(TAG, "SHA256 download HTTP %d", code);
        https.end();
        return false;
    }
    Stream& stream = https.getStream();
    size_t  len    = 0;
    while (https.connected() && len + 1 < hexOutLen) {
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
        hexOut[len++] = static_cast<char>(c);
    }
    hexOut[len] = '\0';
    https.end();
    return len >= 64;
}

bool httpStreamFirmwareToOtaVerified(WiFiClientSecure& tls, const char* binUrl,
                                     const std::array<uint8_t, 32>& expectedHash) {
    HTTPClient https;
    if (!https.begin(tls, binUrl)) {
        ESP_LOGE(TAG, "Firmware URL: HTTPS begin failed");
        return false;
    }
    https.setConnectTimeout(20000);
    https.setTimeout(60000);
    https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    const int httpCode = https.GET();
    if (httpCode != HTTP_CODE_OK) {
        ESP_LOGE(TAG, "Firmware download HTTP %d", httpCode);
        https.end();
        return false;
    }

    const esp_partition_t* updatePart = esp_ota_get_next_update_partition(nullptr);
    if (updatePart == nullptr) {
        ESP_LOGE(TAG, "No OTA partition available");
        https.end();
        return false;
    }

    EspOtaSession        otaSess{};
    MbedtlsSha256Session sha{};
    esp_err_t            err =
        esp_ota_begin(updatePart, OTA_WITH_SEQUENTIAL_WRITES, otaSess.addr());
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin: %s", esp_err_to_name(err));
        https.end();
        return false;
    }

    Stream&             stream       = https.getStream();
    size_t              totalWritten = 0;
    const int           contentLen   = https.getSize();
    const unsigned long startMs      = millis();

    const auto abandonOtaSilent = [&]() {
        otaSess.abandon();
    };

    auto streamBodyShaAndWrite = [&](void) -> bool {
        if (contentLen > 0) {
            int remain = contentLen;
            while (remain > 0) {
                chayaTaskWatchdogReset();
                if (millis() - startMs > 300000UL) {
                    abandonOtaSilent();
                    ESP_LOGE(TAG, "Firmware download timed out");
                    return false;
                }
                if (stream.available() <= 0) {
                    if (!https.connected()) {
                        break;
                    }
                    delay(1);
                    continue;
                }
                const int take =
                    std::min(remain, static_cast<int>(g_otaFwReadChunk.size()));
                const size_t n =
                    stream.readBytes(g_otaFwReadChunk.data(), static_cast<size_t>(take));
                if (n == 0) {
                    break;
                }
                mbedtls_sha256_update(sha.ptr(), g_otaFwReadChunk.data(), n);
                err = esp_ota_write(*otaSess.addr(), g_otaFwReadChunk.data(), n);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "esp_ota_write: %s", esp_err_to_name(err));
                    abandonOtaSilent();
                    return false;
                }
                totalWritten += n;
                remain -= static_cast<int>(n);
            }
            if (remain != 0) {
                abandonOtaSilent();
                ESP_LOGE(TAG, "Firmware truncated");
                return false;
            }
            return true;
        }
        while (https.connected() || stream.available() > 0) {
            chayaTaskWatchdogReset();
            if (millis() - startMs > 300000UL) {
                abandonOtaSilent();
                ESP_LOGE(TAG, "Firmware download timed out");
                return false;
            }
            if (stream.available() <= 0) {
                if (!https.connected()) {
                    break;
                }
                delay(1);
                continue;
            }
            const size_t n =
                stream.readBytes(g_otaFwReadChunk.data(), g_otaFwReadChunk.size());
            if (n == 0) {
                break;
            }
            mbedtls_sha256_update(sha.ptr(), g_otaFwReadChunk.data(), n);
            err = esp_ota_write(*otaSess.addr(), g_otaFwReadChunk.data(), n);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "esp_ota_write: %s", esp_err_to_name(err));
                abandonOtaSilent();
                return false;
            }
            totalWritten += n;
        }
        return true;
    };

    const bool streamedOk = streamBodyShaAndWrite();

    https.end();

    if (!streamedOk) {
        return false;
    }

    sha.disarmNoFree();

    unsigned char computedRaw[32];
    mbedtls_sha256_finish(sha.ptr(), computedRaw);
    mbedtls_sha256_free(sha.ptr());

    std::array<uint8_t, 32> computed{};
    memcpy(computed.data(), computedRaw, 32);

    if (memcmp(computed.data(), expectedHash.data(), 32) != 0) {
        ESP_LOGE(TAG, "SHA256 mismatch — rejecting image (written=%u)",
                 static_cast<unsigned>(totalWritten));
        otaSess.abandon();
        return false;
    }

    err = esp_ota_end(*otaSess.addr());
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end: %s", esp_err_to_name(err));
        abandonOtaSilent();
        return false;
    }

    otaSess.disarmCommitted();

    err = esp_ota_set_boot_partition(updatePart);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "OTA verified, %u bytes", static_cast<unsigned>(totalWritten));
    return true;
}

} // namespace

bool otaFlashVerifiedInstall(const char* binUrl) {
    constexpr size_t kOtaUrlMax = 256;
    char shaUrl[kOtaUrlMax];
    if (!otaBinUrlToSha256Url(binUrl, shaUrl, sizeof(shaUrl))) {
        ESP_LOGE(TAG, "Cannot derive .sha256 URL from firmware URL");
        return false;
    }

    WiFiClientSecure tls;
    tls.setCACertBundle(x509_crt_bundle_start,
                        static_cast<size_t>(x509_crt_bundle_end - x509_crt_bundle_start));
    tls.setTimeout(60000);

    char hexBuf[96];
    hexBuf[0] = '\0';
    if (!httpFetchSha256Hex(tls, shaUrl, hexBuf, sizeof(hexBuf))) {
        ESP_LOGE(TAG, "Failed to download SHA256 sidecar");
        return false;
    }

    std::array<uint8_t, 32> expected{};
    if (!parseHexSha256(hexBuf, expected)) {
        ESP_LOGE(TAG, "SHA256 ASCII parse failed");
        return false;
    }

    return httpStreamFirmwareToOtaVerified(tls, binUrl, expected);
}
