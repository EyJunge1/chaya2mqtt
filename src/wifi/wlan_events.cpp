#include "wlan_config.h"
#include "wlan_internal.h"

#include "async/event_types.h"
#include "async/task_handles.h"

#include <Arduino.h>
#include <WiFi.h>
#include <algorithm>
#include <esp_log.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "util/log_tag.h"

DEFINE_LOG_TAG("WIFI");

static void queueWifiReconnect() {
    s_staReconnectWorkPending.store(true, std::memory_order_release);
    if (g_netCmdQueue == nullptr) {
        return;
    }
    const NetCmd cmd = NetCmd::WifiReconnect;
    if (xQueueSend(g_netCmdQueue, &cmd, 0) != pdTRUE) {
        // wlanLoop() consumes s_staReconnectWorkPending even if this wake-up is lost.
        ESP_LOGD(TAG, "netCmd queue full (WifiReconnect) — coalesced");
    }
}

static void queueWifiGotIp() {
    s_staGotIpWorkPending.store(true, std::memory_order_release);
    if (g_netCmdQueue == nullptr) {
        return;
    }
    const NetCmd cmd = NetCmd::WifiGotIp;
    if (xQueueSend(g_netCmdQueue, &cmd, 0) != pdTRUE) {
        // wlanLoop() consumes s_staGotIpWorkPending even if this wake-up is lost.
        ESP_LOGD(TAG, "netCmd queue full (WifiGotIp) — coalesced");
    }
}

void wlanHandleStaReconnectNetCmd() {
    if (!s_staReconnectWorkPending.exchange(false, std::memory_order_acq_rel)) {
        return;
    }
    if (g_apMode.load(std::memory_order_relaxed)) {
        return;
    }
    if (s_epdRefreshActive.load(std::memory_order_acquire)) {
        // Keep the request alive; wlanEndLowInterferenceForEpd() re-wakes the network task.
        s_staReconnectWorkPending.store(true, std::memory_order_release);
        ESP_LOGD(TAG, "WLAN reconnect deferred (EPD refresh)");
        return;
    }
    const unsigned long nowMs       = millis();
    const unsigned long nextAllowed = s_wifiReconnectNextAllowedMs.load(std::memory_order_relaxed);
    if (nextAllowed != 0UL && static_cast<std::int32_t>(nowMs - nextAllowed) < 0) {
        // Keep the coalesced request alive; wlanLoop() retries when the backoff expires.
        s_staReconnectWorkPending.store(true, std::memory_order_release);
        ESP_LOGD(TAG, "WLAN reconnect skipped (backoff)");
        return;
    }

    const uint32_t fails = s_wifiReconnectFailCount.load(std::memory_order_relaxed);
    const uint8_t  reason =
        s_lastStaDisconnectReason.load(std::memory_order_relaxed);

    if (fails >= kWifiSoftReconnectAttemptsBeforeForce) {
        ESP_LOGW(TAG, "WLAN soft reconnect exhausted (fails=%u reason=%u) — force reassoc",
                 static_cast<unsigned>(fails), static_cast<unsigned>(reason));
        wlanForceStaReassoc("event-escalate");
    } else {
        ESP_LOGW(TAG, "WLAN disconnected, soft reconnect (fails=%u reason=%u)...",
                 static_cast<unsigned>(fails), static_cast<unsigned>(reason));
        wlanWifiApiLock();
        const bool connected = WiFi.status() == WL_CONNECTED && WiFi.localIP()[0] != 0;
        if (connected) {
            wlanWifiApiUnlock();
            ESP_LOGD(TAG, "Stale WLAN reconnect skipped: STA already connected");
            return;
        }
        const wifi_mode_t mode = WiFi.getMode();
        if (mode == WIFI_STA || mode == WIFI_AP_STA) {
            // Unlike WiFi.reconnect(), this never disconnects a connection that won the race.
            const esp_err_t err = esp_wifi_connect();
            if (err != ESP_OK) {
                ESP_LOGD(TAG, "esp_wifi_connect: %s", esp_err_to_name(err));
            }
        }
        wlanWifiApiUnlock();
    }

    const uint32_t shift =
        std::min(fails, static_cast<uint32_t>(6));
    const unsigned long backoff =
        std::min(kWifiReconnectBaseBackoffMs * (1UL << shift), kWifiReconnectMaxBackoffMs);
    s_wifiReconnectFailCount.fetch_add(1, std::memory_order_relaxed);
    s_wifiReconnectNextAllowedMs.store(nowMs + backoff, std::memory_order_relaxed);
}

void wifiStationEvent(arduino_event_id_t event, arduino_event_info_t info) {
    if (g_apMode.load(std::memory_order_relaxed)) {
        return;
    }
    switch (event) {
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
        s_staLastGotIpWallMs.store(0UL, std::memory_order_relaxed);
        const uint8_t reason = info.wifi_sta_disconnected.reason;
        s_lastStaDisconnectReason.store(reason, std::memory_order_relaxed);
        const uint8_t* bssid = info.wifi_sta_disconnected.bssid;
        ESP_LOGW(TAG,
                 "STA_DISCONNECTED reason=%u ssid='%.32s' rssi=%d "
                 "bssid=%02x:%02x:%02x:%02x:%02x:%02x",
                 static_cast<unsigned>(reason),
                 reinterpret_cast<const char*>(info.wifi_sta_disconnected.ssid),
                 static_cast<int>(info.wifi_sta_disconnected.rssi), bssid[0], bssid[1], bssid[2],
                 bssid[3], bssid[4], bssid[5]);
        if (!s_wifiSetupComplete.load(std::memory_order_acquire)) {
            break;
        }
        queueWifiReconnect();
        break;
    }
    case ARDUINO_EVENT_WIFI_STA_LOST_IP: {
        s_staLastGotIpWallMs.store(0UL, std::memory_order_relaxed);
        // Synthetic reason: treat like disconnect for recovery / escalate path.
        s_lastStaDisconnectReason.store(200U, std::memory_order_relaxed);
        ESP_LOGW(TAG, "STA_LOST_IP — queue reconnect");
        if (!s_wifiSetupComplete.load(std::memory_order_acquire)) {
            break;
        }
        queueWifiReconnect();
        break;
    }
    case ARDUINO_EVENT_WIFI_STA_GOT_IP: {
        ESP_LOGD(TAG, "GOT_IP callback core=%d", static_cast<int>(xPortGetCoreID()));
        s_staLastGotIpWallMs.store(millis(), std::memory_order_relaxed);
        s_wifiReconnectFailCount.store(0U, std::memory_order_relaxed);
        s_wifiReconnectNextAllowedMs.store(0UL, std::memory_order_relaxed);
        s_lastStaDisconnectReason.store(0U, std::memory_order_relaxed);
        s_staReconnectWorkPending.store(false, std::memory_order_release);
        queueWifiGotIp();
        break;
    }
    default:
        break;
    }
}
