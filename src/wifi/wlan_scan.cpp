#include "wlan.h"

#include "wlan_config.h"
#include "wlan_internal.h"

#include "util/log_tag.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiType.h>
#include <algorithm>
#include <esp_log.h>
#include <esp_wifi.h>

DEFINE_LOG_TAG("WIFI");

void wlanRequestWifiScanRefresh() {
    const unsigned long now  = millis();
    const unsigned long last = s_lastWifiScanKickMs.load(std::memory_order_relaxed);
    if (last != 0UL && (now - last) < kWifiScanKickMinIntervalMs) {
        return;
    }
    s_lastWifiScanKickMs.store(now, std::memory_order_relaxed);
    s_wifiScanKick.store(true, std::memory_order_release);
}

void wifiScanStopForEpdLocked() {
    if (!s_wifiScanInProgress.exchange(false, std::memory_order_acq_rel)) {
        return;
    }
    (void)esp_wifi_scan_stop();
    WiFi.scanDelete();
    // Preserve the user's request; restart after the EPD window closes.
    s_wifiScanKick.store(true, std::memory_order_release);
    ESP_LOGI(TAG, "WLAN scan paused for EPD refresh");
}

bool wlanWifiScanCacheReady() {
    return s_wifiScanHasValidCache.load(std::memory_order_acquire)
           && !s_wifiScanInProgress.load(std::memory_order_acquire);
}

size_t wlanWifiScanCopySnapshot(WlanScanRow* out, size_t maxRows) {
    if (out == nullptr || maxRows == 0U) {
        return 0;
    }
    portENTER_CRITICAL(&s_wifiScanCacheMux);
    const size_t n = std::min(maxRows, s_wifiScanCacheCount);
    for (size_t i = 0; i < n; ++i) {
        out[i] = s_wifiScanCache[i];
    }
    portEXIT_CRITICAL(&s_wifiScanCacheMux);
    return n;
}

size_t wlanWifiScanCachedCount() {
    portENTER_CRITICAL(&s_wifiScanCacheMux);
    const size_t n = s_wifiScanCacheCount;
    portEXIT_CRITICAL(&s_wifiScanCacheMux);
    return n;
}

bool wlanWifiScanCopyRowAt(size_t index, WlanScanRow* out) {
    if (out == nullptr) {
        return false;
    }
    portENTER_CRITICAL(&s_wifiScanCacheMux);
    if (index >= s_wifiScanCacheCount) {
        portEXIT_CRITICAL(&s_wifiScanCacheMux);
        return false;
    }
    *out = s_wifiScanCache[index];
    portEXIT_CRITICAL(&s_wifiScanCacheMux);
    return true;
}

void wifiScanServiceOnMainTask() {
    wlanWifiApiLock();
    // Recheck under the WiFi mutex: the display task may have opened the EPD
    // window after wlanLoop() took its initial state snapshot.
    if (s_epdRefreshActive.load(std::memory_order_acquire)) {
        wlanWifiApiUnlock();
        return;
    }
    if (s_wifiScanKick.exchange(false, std::memory_order_acq_rel)) {
        const unsigned long nowMs       = millis();
        const unsigned long nextAllowed = s_wifiScanNextAllowedMs.load(std::memory_order_relaxed);
        if (nextAllowed != 0UL && nowMs < nextAllowed) {
            s_wifiScanKick.store(true, std::memory_order_release);
            wlanWifiApiUnlock();
            return;
        }
        WiFi.scanDelete();
        s_wifiScanInProgress.store(true, std::memory_order_release);
        s_wifiScanHasValidCache.store(false, std::memory_order_release);
        ESP_LOGI(TAG, "WLAN scan started");
        // 120 ms/channel keeps a full 2.4 GHz sweep inside the UI poll window.
        WiFi.scanNetworks(true, false, false, 120, 0, nullptr, nullptr);
    }

    if (!s_wifiScanInProgress.load(std::memory_order_acquire)) {
        wlanWifiApiUnlock();
        return;
    }

    const int16_t n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) {
        wlanWifiApiUnlock();
        return;
    }
    if (n == WIFI_SCAN_FAILED) {
        ESP_LOGW(TAG, "WLAN scan failed");
        WiFi.scanDelete();
        s_wifiScanInProgress.store(false, std::memory_order_release);
        s_wifiScanHasValidCache.store(false, std::memory_order_release);
        s_wifiScanNextAllowedMs.store(millis() + kWifiScanFailBackoffMs, std::memory_order_relaxed);
        wlanWifiApiUnlock();
        return;
    }
    if (n < 0) {
        s_wifiScanInProgress.store(false, std::memory_order_release);
        wlanWifiApiUnlock();
        return;
    }

    const size_t usable = std::min(static_cast<size_t>(n), kWlanWifiScanCacheMaxRows);
    size_t       rowCount = usable;
    uint16_t     apCount  = static_cast<uint16_t>(usable);
    wifi_ap_record_t records[kWlanWifiScanCacheMaxRows]{};
    const esp_err_t recErr = esp_wifi_scan_get_ap_records(&apCount, records);
    if (recErr == ESP_OK && apCount > 0U) {
        rowCount = std::min(static_cast<size_t>(apCount), usable);
        for (size_t i = 0; i < rowCount; ++i) {
            s_wifiScanRowWork[i].rssi = records[i].rssi;
            s_wifiScanRowWork[i].open = (records[i].authmode == WIFI_AUTH_OPEN);
            strlcpy(s_wifiScanRowWork[i].ssid, reinterpret_cast<const char*>(records[i].ssid),
                    sizeof(s_wifiScanRowWork[i].ssid));
        }
    } else {
        for (size_t i = 0; i < usable; ++i) {
            const uint8_t idx = static_cast<uint8_t>(i);
            s_wifiScanRowWork[i].rssi = static_cast<int>(WiFi.RSSI(idx));
            s_wifiScanRowWork[i].open = (WiFi.encryptionType(idx) == WIFI_AUTH_OPEN);
            const String ssidStr      = WiFi.SSID(idx);
            strlcpy(s_wifiScanRowWork[i].ssid, ssidStr.c_str(), sizeof(s_wifiScanRowWork[i].ssid));
        }
    }
    portENTER_CRITICAL(&s_wifiScanCacheMux);
    s_wifiScanCacheCount = rowCount;
    for (size_t i = 0; i < rowCount; ++i) {
        s_wifiScanCache[i] = s_wifiScanRowWork[i];
    }
    portEXIT_CRITICAL(&s_wifiScanCacheMux);
    WiFi.scanDelete();
    s_wifiScanInProgress.store(false, std::memory_order_release);
    s_wifiScanHasValidCache.store(true, std::memory_order_release);
    ESP_LOGI(TAG, "WLAN scan done, %u AP(s)", static_cast<unsigned>(rowCount));
    wlanWifiApiUnlock();
}
