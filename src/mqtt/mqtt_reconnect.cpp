#include "mqtt.h"

#include "mqtt_internal.h"

#include "backoff.h"
#include "config.h"
#include "wifi/wlan.h"

#include <Arduino.h>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <mqtt_client.h>

#include "util/log_tag.h"

DEFINE_LOG_TAG("MQTT");

unsigned long mqttConnectPrecheckDeferMs() {
    const bool brokerOk = mqttCfgIsBrokerConfigured();
    const bool wifiOk = wlanStaConnectedOk();
    const bool stableOk = wlanStaStableForMqtt();
    const bool ntpOk = wlanNtpSynced();
    const unsigned long defer = mqttConnectPrecheckDeferMsPure(brokerOk, wifiOk, stableOk, ntpOk);
    if (!brokerOk) {
        ESP_LOGW(TAG, "No MQTT broker configured — use setup AP or /mqtt on the provisioning network");
    } else if (!wifiOk) {
        ESP_LOGD(TAG, "WiFi not connected, deferring MQTT attempt");
    } else if (!stableOk) {
        ESP_LOGD(TAG, "WiFi not stable yet after GOT_IP, deferring MQTT attempt");
    } else if (!ntpOk) {
        ESP_LOGI(TAG, "NTP not synced — deferring MQTT/TLS (retry in %lu ms)", static_cast<unsigned long>(kMqttNtpRetryMs));
    }
    return defer;
}

void mqttDisconnect() { mqttKillClient(); }

void mqttRequestKillClientDeferred() { s_mqttKillCoalesce.store(true, std::memory_order_release); }

void mqttSetup() {
    ESP_LOGI(TAG, "MQTT setup (kill+reset backoff)");
    mqttKillClient();
    portENTER_CRITICAL(&s_mqttBackoffMux);
    lastMqttAttemptAt = 0;
    mqttBackoffMs = 0;
    mqttCurrentBackoffMs = kMqttBackoffInitialMs;
    portEXIT_CRITICAL(&s_mqttBackoffMux);
}

bool mqttIsConnected() { return s_connected.load(std::memory_order_acquire); }

static void mqttLoopApplyWifiPowerSaveOnConnectChange(bool connected, bool &wasConnected) {
    if (wasConnected && !connected) {
        wlanSetStaPowerSaveMqttActive(false);
    }
    if (connected && !wasConnected) {
        portENTER_CRITICAL(&s_mqttBackoffMux);
        lastMqttAttemptAt = 0;
        mqttBackoffMs = 0;
        mqttCurrentBackoffMs = kMqttBackoffInitialMs;
        portEXIT_CRITICAL(&s_mqttBackoffMux);
        wlanSetStaPowerSaveMqttActive(true);
    }
    wasConnected = connected;
}

static void mqttLoopTryReconnect(MqttConfig &loopCfg, unsigned long now) {
    const bool pending = s_connectPending.load(std::memory_order_acquire);
    const bool connected = mqttIsConnected();
    if (connected || pending) {
        return;
    }

    bool backoffElapsed = false;
    portENTER_CRITICAL(&s_mqttBackoffMux);
    backoffElapsed = mqttBackoffMs == 0UL || ((now - lastMqttAttemptAt) >= mqttBackoffMs);
    if (backoffElapsed) {
        lastMqttAttemptAt = now;
    }
    portEXIT_CRITICAL(&s_mqttBackoffMux);

    if (!backoffElapsed) {
        unsigned long remMs = 0;
        unsigned long periodMs = 0;
        portENTER_CRITICAL(&s_mqttBackoffMux);
        periodMs = mqttBackoffMs;
        if (mqttBackoffMs > 0U) {
            const unsigned long elapsed = now - lastMqttAttemptAt;
            remMs = (elapsed < mqttBackoffMs) ? (mqttBackoffMs - elapsed) : 0UL;
        }
        portEXIT_CRITICAL(&s_mqttBackoffMux);
        (void)remMs;
        (void)periodMs;
        ESP_LOGD(TAG, "Reconnect backoff: %lu ms remaining (period %lu ms)", remMs, periodMs);
        return;
    }

    const unsigned long deferMs = mqttConnectPrecheckDeferMs();
    if (deferMs != 0U) {
        portENTER_CRITICAL(&s_mqttBackoffMux);
        mqttBackoffMs = deferMs;
        portEXIT_CRITICAL(&s_mqttBackoffMux);
        ESP_LOGD(TAG, "MQTT connect deferred by precheck (%lu ms)", deferMs);
        return;
    }

    portENTER_CRITICAL(&s_mqttBackoffMux);
    mqttBackoffMs = 0;
    portEXIT_CRITICAL(&s_mqttBackoffMux);

    ESP_LOGI(TAG, "MQTT start… server %s:%u", loopCfg.server, static_cast<unsigned>(loopCfg.port));

    if (!mqttEnsureClientAllocated()) {
        applyDisconnectFailureBackoff(/*wifiSuspect=*/!wlanStaConnectedOk());
        return;
    }

    if (!mqttClientLock()) {
        ESP_LOGW(TAG, "MQTT start skipped: mutex timeout");
        applyDisconnectFailureBackoff(/*wifiSuspect=*/!wlanStaConnectedOk());
        return;
    }
    s_connectPending.store(true, std::memory_order_release);
    esp_err_t sr = ESP_FAIL;
    if (s_client != nullptr) {
        // Prefer reconnect on an existing handle after unintentional disconnect (PERF-02).
        sr = esp_mqtt_client_reconnect(s_client);
        if (sr != ESP_OK) {
            sr = esp_mqtt_client_start(s_client);
        }
    }
    mqttClientUnlock();

    if (sr != ESP_OK) {
        ESP_LOGE(TAG, "esp_mqtt_client_start/reconnect failed: %s", esp_err_to_name(sr));
        s_connectPending.store(false, std::memory_order_release);
        applyDisconnectFailureBackoff(/*wifiSuspect=*/!wlanStaConnectedOk());
        mqttKillClient();
    }
}

void mqttLoop() {
    if (wlanEpdRefreshActive()) {
        // Keep kill/reconnect work pending; network task must stay short for TWDT.
        return;
    }

    mqttServicePublishAckTimeout();

    if (s_mqttKillCoalesce.exchange(false, std::memory_order_acq_rel)) {
        mqttKillClient();
    }

    static MqttConfig s_loopCfg{};
    static bool s_haveOfflineSnap = false;
    const bool connectedEarly = mqttIsConnected();
    const bool dirty = mqttCfgConsumeDirtySnapshotNeeded();
    // PERF-05: while offline, snapshot only on first entry or dirty config.
    if (dirty || (!connectedEarly && !s_haveOfflineSnap)) {
        mqttCfgSnapshot(&s_loopCfg);
        if (!connectedEarly) {
            s_haveOfflineSnap = true;
        }
    }
    if (connectedEarly) {
        s_haveOfflineSnap = false;
    }

    if (s_loopCfg.server[0] == '\0') {
        if (wlanStaConnectedOk()) {
            wlanSetStaPowerSaveMqttActive(true);
        }
        if (!mqttClientLockTimed()) {
            return;
        }
        const bool hasClient = s_client != nullptr;
        mqttClientUnlock();
        if (hasClient || s_connectPending.load(std::memory_order_acquire)) {
            ESP_LOGW(TAG, "MQTT broker not configured — stopping client");
            mqttKillClient();
        }
        return;
    }

    const unsigned long now = millis();

    static bool wasConnected = false;
    const bool connected = mqttIsConnected();
    mqttLoopApplyWifiPowerSaveOnConnectChange(connected, wasConnected);

    mqttLoopTryReconnect(s_loopCfg, now);
}

void mqttPostponeConnect(unsigned long delayMs) {
    portENTER_CRITICAL(&s_mqttBackoffMux);
    lastMqttAttemptAt = millis();
    mqttBackoffMs = delayMs;
    portEXIT_CRITICAL(&s_mqttBackoffMux);
}
