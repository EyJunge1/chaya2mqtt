#include "wlan_config.h"
#include "wlan_internal.h"

#include "async/event_types.h"
#include "async/task_handles.h"
#include "constants.h"
#include "ip_format.h"

#include <Arduino.h>
#include <WiFi.h>
#include <algorithm>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "log_tag.h"

DEFINE_LOG_TAG("WIFI");

void wlanHandleStaReconnectNetCmd() {
    if (g_apMode.load(std::memory_order_relaxed)) {
        return;
    }
    const unsigned long nowMs       = millis();
    const unsigned long nextAllowed = s_wifiReconnectNextAllowedMs.load(std::memory_order_relaxed);
    if (nextAllowed != 0UL && static_cast<std::int32_t>(nowMs - nextAllowed) < 0) {
        ESP_LOGD(TAG, "WLAN reconnect skipped (backoff)");
        return;
    }
    ESP_LOGW(TAG, "WLAN disconnected, attempting reconnect...");
    wlanWifiApiLock();
    if (WiFi.getMode() == WIFI_STA || WiFi.getMode() == WIFI_AP_STA) {
        WiFi.reconnect();
        const uint32_t shift = std::min(s_wifiReconnectFailCount.load(std::memory_order_relaxed),
                                        static_cast<uint32_t>(6));
        const unsigned long backoff =
            std::min(kWifiReconnectBaseBackoffMs * (1UL << shift), kWifiReconnectMaxBackoffMs);
        s_wifiReconnectFailCount.fetch_add(1, std::memory_order_relaxed);
        s_wifiReconnectNextAllowedMs.store(nowMs + backoff, std::memory_order_relaxed);
    }
    wlanWifiApiUnlock();
}

void wifiStationEvent(arduino_event_id_t event) {
    if (g_apMode.load(std::memory_order_relaxed)) {
        return;
    }
    switch (event) {
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
        s_staLastGotIpWallMs.store(0UL, std::memory_order_relaxed);
        if (!s_wifiSetupComplete.load(std::memory_order_acquire)) {
            break;
        }
        if (g_netCmdQueue != nullptr) {
            const NetCmd cmd = NetCmd::WifiReconnect;
            if (xQueueSend(g_netCmdQueue, &cmd, 0) != pdTRUE) {
                ESP_LOGW(TAG, "netCmd queue full (WifiReconnect)");
            }
        }
        break;
    }
    case ARDUINO_EVENT_WIFI_STA_GOT_IP: {
        s_staLastGotIpWallMs.store(millis(), std::memory_order_relaxed);
        s_wifiReconnectFailCount.store(0U, std::memory_order_relaxed);
        s_wifiReconnectNextAllowedMs.store(0UL, std::memory_order_relaxed);
        if (s_bootStaConnectPending.exchange(false, std::memory_order_acq_rel)) {
            bool expectedFinish = false;
            if (s_bootStaFinishDone.compare_exchange_strong(expectedFinish, true,
                                                           std::memory_order_acq_rel)) {
                setupWifiFinishStaConnected();
            }
            s_bootWifiSettled.store(true, std::memory_order_release);
        }
        char ipStr[16];
        if (!wlanWifiApiLockTimed(2000U)) {
            ESP_LOGW(TAG, "GOT_IP: WiFi API mutex timeout");
            break;
        }
        formatIpv4ToBuf(WiFi.localIP(), ipStr, sizeof(ipStr));
        wlanWifiApiUnlock();
        ESP_LOGI(TAG, "WLAN STA IP: %s", ipStr);
        s_mdnsRestartNeeded.store(true, std::memory_order_release);
        break;
    }
    default:
        break;
    }
}
