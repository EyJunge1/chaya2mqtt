#include <Arduino.h>

#include "mqtt.h"

#include "mqtt_config.h"
#include "tls_bundle.h"
#include "counter.h"
#include "display.h"
#include "wlan.h"
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <algorithm>
#include <atomic>
#include <climits>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <esp_log.h>
#include <esp_random.h>
#include <esp32-hal-cpu.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "log_tag.h"

DEFINE_LOG_TAG("MQTT");

static WiFiClientSecure espClient;
static PubSubClient client(espClient);

/** When NVS has no broker configured, avoid tight reconnect attempts. */
static constexpr unsigned long kMqttBrokerMissingBackoffMs = 60000UL;

static constexpr unsigned long kMqttBackoffInitialMs = 30000;
static constexpr unsigned long kMqttBackoffMaxMs = 60000;
/** Longer wait when STA is down so reconnect/logging is not spammed. */
static constexpr unsigned long kMqttWifiDownBackoffMs = 20000UL;
/** After TLS failure while STA dropped (BEACON_TIMEOUT etc.), wait longer before retry. */
static constexpr unsigned long kMqttWifiLostDuringTlsBackoffMs = 90000UL;

static unsigned long lastMqttAttemptAt = 0;
static unsigned long mqttBackoffMs = 0;
static unsigned long mqttCurrentBackoffMs = kMqttBackoffInitialMs;

/** SNTP must be valid before MQTT/TLS (mbedTLS certificate notBefore/notAfter checks). */
static constexpr unsigned long kMqttNtpRetryMs = 2000UL;

static constexpr uint32_t kMqttTlsBoostCpuMhz = 240;

/** Stack for mbedTLS inside client.connect(); ESP-IDF ulStackDepth is bytes (not FreeRTOS words). */
static constexpr uint32_t kMqttConnectTaskStackBytes = 10240;
static constexpr UBaseType_t kMqttConnectTaskPriority = 5;

enum class MqttConnectPhase : int {
    Idle        = 0,
    InProgress  = 1,
    DoneSuccess = 2,
    DoneFail    = 3,
};

static std::atomic<int> s_connectPhase{static_cast<int>(MqttConnectPhase::Idle)};

struct MqttConnectTaskParams {
    MqttConfig cfg{};
    char       clientId[24]{};
    char       willTopic[140]{};
};

bool mqttIsConnectInProgress() {
    return s_connectPhase.load(std::memory_order_acquire)
           == static_cast<int>(MqttConnectPhase::InProgress);
}

static void mqttConnectTaskFn(void* param) {
    auto* p = static_cast<MqttConnectTaskParams*>(param);
    if (p == nullptr) {
        vTaskDelete(nullptr);
        return;
    }

    /* Reduce beacon loss risk during long TLS on blocked task context. */
    wlanSetStaPowerSaveMqttActive(false);

    setCpuFrequencyMhz(static_cast<int>(kMqttTlsBoostCpuMhz));
    const bool connected =
        client.connect(p->clientId, p->cfg.username, p->cfg.password, p->willTopic, 1, true,
                       "offline", true);
    setCpuFrequencyMhz(80);

    delete p;

    if (connected) {
        s_connectPhase.store(static_cast<int>(MqttConnectPhase::DoneSuccess),
                             std::memory_order_release);
    } else {
        s_connectPhase.store(static_cast<int>(MqttConnectPhase::DoneFail), std::memory_order_release);
    }

    vTaskDelete(nullptr);
}

/**
 * Preconditions only (main loop). Starts TLS connect task or returns backoff ms if deferred/failed to start.
 */
static unsigned long mqttTryConnectPrecheckAndStartTask() {
    const int phase = s_connectPhase.load(std::memory_order_acquire);
    if (phase != static_cast<int>(MqttConnectPhase::Idle)) {
        return mqttBackoffMs;
    }

    MqttConfig cfg{};
    mqttCfgSnapshot(&cfg);

    if (strlen(cfg.server) == 0) {
        ESP_LOGW(TAG,
                 "No MQTT broker configured — use setup AP or /mqtt on the provisioning network");
        return kMqttBrokerMissingBackoffMs;
    }

    if (!wlanStaConnectedOk()) {
        ESP_LOGD(TAG, "WiFi not connected, deferring MQTT attempt");
        return kMqttWifiDownBackoffMs;
    }
    if (!wlanStaStableForMqtt()) {
        ESP_LOGD(TAG, "WiFi not stable yet after GOT_IP, deferring MQTT attempt");
        return kMqttNtpRetryMs;
    }
    if (!wlanNtpSynced()) {
        ESP_LOGI(TAG, "NTP not synced — deferring MQTT/TLS (retry in %lu ms)",
                 static_cast<unsigned long>(kMqttNtpRetryMs));
        return kMqttNtpRetryMs;
    }

    char clientId[24];
    snprintf(clientId, sizeof(clientId), "Chaya2MQTT-%04lX",
             static_cast<unsigned long>(esp_random() & 0xffffU));

    ESP_LOGI(TAG, "Connecting to MQTT over TLS... server %s:%u client %s", cfg.server, cfg.port,
             clientId);

    char willTopic[140];
    snprintf(willTopic, sizeof(willTopic), "%s/lwt", cfg.topicPub);

    if (!wlanStaConnectedOk()) {
        return kMqttWifiDownBackoffMs;
    }

    auto* params = new (std::nothrow) MqttConnectTaskParams{};
    if (params == nullptr) {
        ESP_LOGE(TAG, "MQTT connect task: out of memory for params");
        return kMqttBackoffInitialMs;
    }
    params->cfg = cfg;
    snprintf(params->clientId, sizeof(params->clientId), "%s", clientId);
    snprintf(params->willTopic, sizeof(params->willTopic), "%s", willTopic);

    s_connectPhase.store(static_cast<int>(MqttConnectPhase::InProgress), std::memory_order_release);

    const BaseType_t ok =
        xTaskCreatePinnedToCore(mqttConnectTaskFn, "mqttTlsConn", kMqttConnectTaskStackBytes,
                                params, kMqttConnectTaskPriority, nullptr, 1);
    if (ok != pdPASS) {
        delete params;
        s_connectPhase.store(static_cast<int>(MqttConnectPhase::Idle), std::memory_order_release);
        ESP_LOGE(TAG, "MQTT connect task: xTaskCreatePinnedToCore failed");
        return kMqttBackoffInitialMs;
    }

    return 0;
}

/** Run on main task after DoneSuccess — exclusive PubSubClient access. */
static void mqttFinalizeConnectSuccess() {
    MqttConfig cfg{};
    mqttCfgSnapshot(&cfg);

    char willTopic[140];
    snprintf(willTopic, sizeof(willTopic), "%s/lwt", cfg.topicPub);

    ESP_LOGI(TAG, "MQTT connected; subscribing (QoS 1): %s", cfg.topicSub);
    (void)client.publish(willTopic, "online", true);
    mqttCurrentBackoffMs = kMqttBackoffInitialMs;
    if (!client.subscribe(cfg.topicSub, 1)) {
        ESP_LOGE(TAG, "MQTT subscribe failed — disconnecting for retry");
        client.disconnect();
        const unsigned long waitMs = mqttCurrentBackoffMs;
        mqttCurrentBackoffMs       = std::min(mqttCurrentBackoffMs * 2UL, kMqttBackoffMaxMs);
        mqttBackoffMs              = waitMs;
        lastMqttAttemptAt          = millis();
        ESP_LOGI(TAG, "Next connect attempt in %lu s (subscribe error)", waitMs / 1000UL);
    }
}

/** Run on main task after DoneFail — exclusive PubSubClient access. */
static void mqttFinalizeConnectFailure() {
    ESP_LOGE(TAG, "MQTT connect failed, PubSubClient state rc=%d", client.state());

    unsigned long waitMs = mqttCurrentBackoffMs;
    if (!wlanStaConnectedOk()) {
        waitMs = std::max(waitMs, kMqttWifiLostDuringTlsBackoffMs);
        ESP_LOGW(TAG, "Wi-Fi not healthy after TLS attempt — backing off %lu s", waitMs / 1000UL);
    }

    mqttCurrentBackoffMs = std::min(mqttCurrentBackoffMs * 2UL, kMqttBackoffMaxMs);
    mqttBackoffMs        = waitMs;
    lastMqttAttemptAt    = millis();
    ESP_LOGI(TAG, "Next connect attempt in %lu s (exponential backoff)", waitMs / 1000UL);
}

// NOLINTNEXTLINE(readability-non-const-parameter) - PubSubClient callback signature is fixed
static void mqttCallback(char* topic, byte* payload, unsigned int length) {
    MqttConfig cfg{};
    mqttCfgSnapshot(&cfg);
    if (strcmp(topic, cfg.topicSub) != 0) {
        ESP_LOGD(TAG, "Ignoring MQTT payload (wrong topic)");
        return;
    }
    if (length == 0 || length > 10U) {
        ESP_LOGD(TAG, "Invalid counter payload length %u", length);
        return;
    }

    char buf[16];
    memcpy(buf, payload, length);
    buf[length] = '\0';

    char* endPtr        = nullptr;
    errno               = 0;
    const long parsed   = strtol(buf, &endPtr, 10);
    /* On ESP32, long == int; ERANGE still catches overflow from strtol. */
    if (errno == ERANGE || endPtr != buf + length || parsed < 0
        || parsed > static_cast<long>(INT_MAX)) {
        ESP_LOGD(TAG, "Counter payload is not a plain integer");
        return;
    }

    const int newCounter = static_cast<int>(parsed);
    if (newCounter == heartCounter) {
        return;
    }

    ESP_LOGI(TAG, "Heart counter from MQTT (remote): %d", newCounter);
    heartCounter = newCounter;
    requestHeartRedraw();
}

bool mqttPublishChaya() {
    if (mqttIsConnectInProgress()) {
        ESP_LOGW(TAG, "Publish skipped: TLS connect task running");
        return false;
    }
    if (!client.connected()) {
        ESP_LOGW(TAG, "Publish skipped: not connected");
        return false;
    }
    MqttConfig cfg{};
    mqttCfgSnapshot(&cfg);

    char buf[16];
    const long nextVal = static_cast<long>(heartSentCounter) + 1L;
    if (nextVal > INT_MAX) {
        ESP_LOGW(TAG, "Publish skipped: heartSentCounter at maximum");
        return false;
    }
    static_cast<void>(snprintf(buf, sizeof(buf), "%ld", nextVal));
    return client.publish(cfg.topicPub, buf, true);
}

void mqttDisconnect() {
    constexpr unsigned long kWaitMs = 15000UL;
    const unsigned long     t0      = millis();
    while (mqttIsConnectInProgress() && (millis() - t0 < kWaitMs)) {
        delay(20);
    }
    client.disconnect();
    s_connectPhase.store(static_cast<int>(MqttConnectPhase::Idle), std::memory_order_release);
}

void mqttSetup() {
    /*
     * PubSubClient::setServer stores only a pointer to the hostname string — it must not point at a
     * local stack buffer. Use the global mqttCfg buffers (stable for program lifetime).
     */
    espClient.setCACertBundle(x509_crt_bundle_start,
                              x509_crt_bundle_end - x509_crt_bundle_start);
    /* TCP connect/read/write timeouts on the socket (ms); TLS runs in separate task — keep moderate. */
    espClient.setConnectionTimeout(3000);
    espClient.setHandshakeTimeout(5);
    if (!client.setBufferSize(512)) {
        ESP_LOGW(TAG, "setBufferSize(512) failed — using existing PubSubClient buffer");
    }
    client.setServer(mqttCfg.server, mqttCfg.port);
    client.setCallback(mqttCallback);
    // E-Paper Full-Refresh blockiert ~8s; längeres Keep-Alive verhindert Broker-Timeout bei zwei Refreshes hintereinander.
    client.setKeepAlive(60);
    client.setSocketTimeout(5);
    lastMqttAttemptAt      = 0;
    mqttBackoffMs          = 0;
    mqttCurrentBackoffMs   = kMqttBackoffInitialMs;
    s_connectPhase.store(static_cast<int>(MqttConnectPhase::Idle), std::memory_order_release);
}

bool mqttIsConnected() {
    if (mqttIsConnectInProgress()) {
        return false;
    }
    return client.connected();
}

void mqttLoop() {
    static MqttConfig s_loopCfg{};
    const bool        inProgEarly    = mqttIsConnectInProgress();
    const bool        connectedEarly = !inProgEarly && client.connected();
    if (!connectedEarly || mqttCfgConsumeDirtySnapshotNeeded()) {
        mqttCfgSnapshot(&s_loopCfg);
    }
    if (s_loopCfg.server[0] == '\0') {
        return;
    }

    const int phaseDone = s_connectPhase.load(std::memory_order_acquire);
    if (phaseDone == static_cast<int>(MqttConnectPhase::DoneSuccess)) {
        mqttFinalizeConnectSuccess();
        s_connectPhase.store(static_cast<int>(MqttConnectPhase::Idle), std::memory_order_release);
    } else if (phaseDone == static_cast<int>(MqttConnectPhase::DoneFail)) {
        mqttFinalizeConnectFailure();
        s_connectPhase.store(static_cast<int>(MqttConnectPhase::Idle), std::memory_order_release);
    }

    const bool inProg    = mqttIsConnectInProgress();
    const bool connected = !inProg && client.connected();

    const unsigned long now = millis();

    static bool wasConnected = false;
    if (wasConnected && !connected) {
        wlanSetStaPowerSaveMqttActive(false);
    }
    if (connected && !wasConnected) {
        lastMqttAttemptAt    = 0;
        mqttBackoffMs        = 0;
        mqttCurrentBackoffMs = kMqttBackoffInitialMs;
        wlanSetStaPowerSaveMqttActive(true);
    }
    wasConnected = connected;

    if (!connected) {
        if (!inProg && (now - lastMqttAttemptAt >= mqttBackoffMs)) {
            lastMqttAttemptAt = now;
            mqttBackoffMs     = mqttTryConnectPrecheckAndStartTask();
        }
    } else {
        client.loop();
    }
}

unsigned long mqttMillisUntilNextConnectAttempt() {
    if (mqttIsConnectInProgress()) {
        /* Prefer short wakeups while TLS handshake runs (main uses mqttIsConnectInProgress guard too). */
        return 500UL;
    }
    if (client.connected()) {
        return 0;
    }
    const unsigned long now     = millis();
    const unsigned long elapsed = now - lastMqttAttemptAt;
    if (elapsed >= mqttBackoffMs) {
        return 0;
    }
    return mqttBackoffMs - elapsed;
}

void mqttPostponeConnect(unsigned long delayMs) {
    lastMqttAttemptAt = millis();
    mqttBackoffMs     = delayMs;
}
