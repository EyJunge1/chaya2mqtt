#include <Arduino.h>

#include "mqtt.h"

#include "async/event_types.h"
#include "async/task_handles.h"
#include "config.h"
#include "constants.h"
#include "tls_bundle.h"
#include "heart/counter.h"
#include "display/display.h"
#include "wifi/wlan.h"

#include <algorithm>
#include <atomic>
#include <climits>
#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <esp_crt_bundle.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/portmacro.h>
#include <mqtt_client.h>

#include "log_tag.h"

DEFINE_LOG_TAG("MQTT");

// Backoff tuning (no broker / WiFi down / TLS vs NTP).
static constexpr unsigned long kMqttBrokerMissingBackoffMs       = 60000UL;
static constexpr unsigned long kMqttBackoffInitialMs             = 30000UL;
static constexpr unsigned long kMqttBackoffMaxMs                 = 60000UL;
static constexpr unsigned long kMqttWifiDownBackoffMs            = 20000UL;
static constexpr unsigned long kMqttWifiLostDuringTlsBackoffMs   = 90000UL;
static constexpr unsigned long kMqttNtpRetryMs                   = 2000UL;

// Teardown/restart client from mqttLoop / network task (not from MQTT callback).
static esp_mqtt_client_handle_t s_client = nullptr;
static std::atomic<bool>        s_connected{false};
static std::atomic<bool> s_connectPending{false};
static std::atomic<bool> s_disconnectIntentional{false};

// Backoff state: touch only under s_mqttBackoffMux or helper fns.
static unsigned long lastMqttAttemptAt     = 0;
static unsigned long mqttBackoffMs         = 0;
static unsigned long mqttCurrentBackoffMs  = kMqttBackoffInitialMs;
static portMUX_TYPE    s_mqttBackoffMux    = portMUX_INITIALIZER_UNLOCKED;

static char s_clientIdBuf[24]{};
static char s_lwtTopicBuf[sizeof(MqttConfig::topicPub) + 16U]{};

// Subscribe topic from last CONNECT (fast DATA path without full cfg copy).
static char     s_mqttSubTopicCache[sizeof(MqttConfig::topicSub)]{};
static size_t   s_mqttSubTopicLen = 0;
static portMUX_TYPE s_mqttSubTopicMux = portMUX_INITIALIZER_UNLOCKED;

static inline void mqttClientLock() {
    if (g_mqttClientMutex != nullptr) {
        xSemaphoreTake(g_mqttClientMutex, portMAX_DELAY);
    }
}

static inline void mqttClientUnlock() {
    if (g_mqttClientMutex != nullptr) {
        xSemaphoreGive(g_mqttClientMutex);
    }
}

static void mqttKillClientImpl();

static void applyDisconnectFailureBackoff(bool wifiSuspectDuringFailure) {
    unsigned long curBackoff = 0;
    portENTER_CRITICAL(&s_mqttBackoffMux);
    curBackoff = mqttCurrentBackoffMs;
    portEXIT_CRITICAL(&s_mqttBackoffMux);

    unsigned long waitMs = curBackoff;
    if (wifiSuspectDuringFailure || !wlanStaConnectedOk()) {
        waitMs = std::max(waitMs, kMqttWifiLostDuringTlsBackoffMs);
        ESP_LOGW(TAG, "Wi-Fi not healthy after MQTT disconnect — backing off %lu s", waitMs / 1000UL);
    }
    portENTER_CRITICAL(&s_mqttBackoffMux);
    mqttCurrentBackoffMs = std::min(curBackoff * 2UL, kMqttBackoffMaxMs);
    mqttBackoffMs        = waitMs;
    lastMqttAttemptAt    = millis();
    portEXIT_CRITICAL(&s_mqttBackoffMux);
    ESP_LOGI(TAG, "Next connect attempt in %lu s", waitMs / 1000UL);
}

// 0 = ready to connect; else wait this many ms.
static unsigned long mqttConnectPrecheckDeferMs() {
    if (!mqttCfgIsBrokerConfigured()) {
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
    return 0;
}

static void mqttQueueKillClientFromEvent() {
    if (g_netCmdQueue == nullptr) {
        return;
    }
    const NetCmd cmd = NetCmd::MqttKillClient;
    if (xQueueSend(g_netCmdQueue, &cmd, pdMS_TO_TICKS(50)) != pdTRUE) {
        ESP_LOGW(TAG, "netCmd queue full (MqttKillClient)");
    }
}

static void handleCounterPayload(const char* payload, unsigned int length) {
    if (length == 0 || length > 10U) {
        ESP_LOGD(TAG, "Invalid counter payload length %u", length);
        return;
    }

    char buf[12];
    memcpy(buf, payload, length);
    buf[length] = '\0';

    char*     endPtr = nullptr;
    errno              = 0;
    const long parsed  = strtol(buf, &endPtr, 10);
    if (errno == ERANGE || endPtr != buf + length || parsed < 0
        || parsed > static_cast<long>(INT_MAX)) {
        ESP_LOGD(TAG, "Counter payload is not a plain integer");
        return;
    }

    const int newCounter = static_cast<int>(parsed);
    if (newCounter == heartCounter.load(std::memory_order_relaxed)) {
        return;
    }

    ESP_LOGI(TAG, "Heart counter from MQTT (remote): %d", newCounter);
    heartCounter.store(newCounter, std::memory_order_relaxed);
    requestHeartRedraw();
}

// Reassemble fragmented counter payload (esp_mqtt may split DATA).
// IMPORTANT: Handler runs in the MQTT client task; esp_mqtt invokes this sequentially per client, so one
// static reassembly buffer is safe. Do not use from multiple MQTT clients concurrently without scoping state.
static bool feedFragmentedPayload(esp_mqtt_event_handle_t ev) {
    static char     accBuf[16];
    static uint32_t expectTotal = 0;
    static unsigned have        = 0;

    if (ev == nullptr || ev->data == nullptr || ev->data_len <= 0) {
        return false;
    }

    // New fragment stream: first chunk has topic + total_len.
    if (ev->topic != nullptr && ev->topic_len > 0 && ev->current_data_offset == 0) {
        have        = 0;
        expectTotal = static_cast<uint32_t>(ev->total_data_len);
        const uint32_t kMaxStored = sizeof(accBuf) - 1U;
        if (expectTotal == 0U || expectTotal > kMaxStored) {
            ESP_LOGD(TAG, "Ignoring MQTT fragment (unexpected total_len=%" PRIu32 ")", expectTotal);
            expectTotal = 0;
            have        = 0;
            return false;
        }
    }

    if (expectTotal == 0U) {
        return false;
    }

    const unsigned add = static_cast<unsigned>(ev->data_len);
    if (have + add > sizeof(accBuf) - 1U) {
        have        = 0;
        expectTotal = 0;
        return false;
    }
    memcpy(accBuf + have, ev->data, add);
    have += add;

    if (have < expectTotal) {
        return false;
    }

    handleCounterPayload(accBuf, expectTotal);
    have        = 0;
    expectTotal = 0;
    return true;
}

static bool topicMatchesSubscribe(const esp_mqtt_event_handle_t ev) {
    if (ev->topic == nullptr || ev->topic_len <= 0) {
        return false;
    }
    portENTER_CRITICAL(&s_mqttSubTopicMux);
    const size_t cachedLen = s_mqttSubTopicLen;
    const bool   match   = (cachedLen > 0U) && (static_cast<size_t>(ev->topic_len) == cachedLen)
                         && (memcmp(ev->topic, s_mqttSubTopicCache, cachedLen) == 0);
    portEXIT_CRITICAL(&s_mqttSubTopicMux);
    return match;
}

static void mqttEventHandler(void* /*handler_args*/, esp_event_base_t /*base*/, int32_t event_id,
                             void* event_data) {
    const esp_mqtt_event_handle_t ev = static_cast<esp_mqtt_event_handle_t>(event_data);
    if (ev == nullptr) {
        return;
    }

    switch (static_cast<esp_mqtt_event_id_t>(event_id)) {
    case MQTT_EVENT_CONNECTED: {
        s_connectPending.store(false, std::memory_order_release);

        MqttConfig cfg{};
        mqttCfgSnapshot(&cfg);
        char lwtPublishTopic[sizeof(s_lwtTopicBuf)];
        static_cast<void>(snprintf(lwtPublishTopic, sizeof(lwtPublishTopic), "%s/lwt", cfg.topicPub));

        portENTER_CRITICAL(&s_mqttSubTopicMux);
        strlcpy(s_mqttSubTopicCache, cfg.topicSub, sizeof(s_mqttSubTopicCache));
        s_mqttSubTopicLen = strlen(s_mqttSubTopicCache);
        portEXIT_CRITICAL(&s_mqttSubTopicMux);

        portENTER_CRITICAL(&s_mqttBackoffMux);
        mqttCurrentBackoffMs = kMqttBackoffInitialMs;
        portEXIT_CRITICAL(&s_mqttBackoffMux);

        ESP_LOGI(TAG, "MQTT connected; subscribing (QoS 1): %s", cfg.topicSub);
        const int mid = esp_mqtt_client_subscribe(ev->client, cfg.topicSub, 1);
        if (mid < 0) {
            ESP_LOGE(TAG, "MQTT subscribe failed — disconnecting for retry");
            (void)esp_mqtt_client_disconnect(ev->client);
            break;
        }

        constexpr const char kOnline[] = "online";
        constexpr int        kOnlineLen  = sizeof(kOnline) - 1;
        if (esp_mqtt_client_publish(ev->client, lwtPublishTopic, kOnline, kOnlineLen, 0, 1)
            < 0) {
            ESP_LOGW(TAG, "MQTT publish retained online failed");
        }

        s_connected.store(true, std::memory_order_release);
        break;
    }
    case MQTT_EVENT_DATA:
        if (topicMatchesSubscribe(ev)) {
            feedFragmentedPayload(ev);
        } else {
            ESP_LOGD(TAG, "Ignoring MQTT payload (wrong topic)");
        }
        break;

    case MQTT_EVENT_DISCONNECTED: {
        s_connected.store(false, std::memory_order_release);
        s_connectPending.store(false, std::memory_order_release);
        // Voluntary mqttKillClient(): skip failure backoff.
        const bool intentional = s_disconnectIntentional.load(std::memory_order_acquire);
        if (!intentional) {
            applyDisconnectFailureBackoff(/*wifiSuspect=*/!wlanStaConnectedOk());
            mqttQueueKillClientFromEvent();
        }
        break;
    }

    case MQTT_EVENT_ERROR:
        if (ev->error_handle != nullptr
            && ev->error_handle->error_type == MQTT_ERROR_TYPE_SUBSCRIBE_FAILED) {
            ESP_LOGE(TAG, "MQTT subscribe rejected by broker");
            (void)esp_mqtt_client_disconnect(ev->client);
        }
        break;

    default:
        break;
    }
}

static esp_err_t installCaBundleLocked() {
    const size_t bundleLen =
        static_cast<size_t>(x509_crt_bundle_end - x509_crt_bundle_start);
    return esp_crt_bundle_set(x509_crt_bundle_start, bundleLen);
}

static bool mqttEnsureClientAllocated() {
    mqttClientLock();
    if (s_client != nullptr) {
        mqttClientUnlock();
        return true;
    }

    MqttConfig cfg{};
    mqttCfgSnapshot(&cfg);

    snprintf(s_clientIdBuf, sizeof(s_clientIdBuf), "Chaya2MQTT-%04lX",
             static_cast<unsigned long>(esp_random() & 0xffffU));

    snprintf(s_lwtTopicBuf, sizeof(s_lwtTopicBuf), "%s/lwt", cfg.topicPub);

    if (installCaBundleLocked() != ESP_OK) {
        ESP_LOGE(TAG, "esp_crt_bundle_set failed");
        mqttClientUnlock();
        return false;
    }

    const char* mqttUser = nullptr;
    if (cfg.username[0] != '\0') {
        mqttUser = cfg.username;
    } else if (cfg.password[0] != '\0') {
        /* Password-only brokers: esp-mqtt needs non-null username pointer. */
        mqttUser = "";
    }
    const char* mqttPassword = (cfg.password[0] != '\0') ? cfg.password : nullptr;

    constexpr const char kOffline[]   = "offline";
    constexpr int        kOfflineLen  = sizeof(kOffline) - 1;
    constexpr int        kWifiNetworkTimeoutMs = 5000;
    constexpr int        kReconnectTimeoutMs   = 60000;

    esp_mqtt_client_config_t mqtt_cfg{};
    mqtt_cfg.broker.address.hostname              = cfg.server;
    mqtt_cfg.broker.address.port                  = cfg.port;
    mqtt_cfg.broker.address.transport             = MQTT_TRANSPORT_OVER_SSL;
    mqtt_cfg.broker.address.uri                   = nullptr;
    mqtt_cfg.broker.address.path                  = nullptr;

    mqtt_cfg.broker.verification.use_global_ca_store           = false;
    mqtt_cfg.broker.verification.crt_bundle_attach               = esp_crt_bundle_attach;
    mqtt_cfg.broker.verification.certificate                     = nullptr;
    mqtt_cfg.broker.verification.certificate_len                  = 0;
    mqtt_cfg.broker.verification.skip_cert_common_name_check    = false;

    mqtt_cfg.credentials.username                               = mqttUser;
    mqtt_cfg.credentials.client_id                              = s_clientIdBuf;
    mqtt_cfg.credentials.set_null_client_id                     = false;
    mqtt_cfg.credentials.authentication.password                = mqttPassword;

    mqtt_cfg.session.disable_clean_session     = false;
    mqtt_cfg.session.keepalive                 = kMqttKeepAliveSeconds;
    mqtt_cfg.session.disable_keepalive        = false;
    mqtt_cfg.session.protocol_ver             = MQTT_PROTOCOL_UNDEFINED;
    mqtt_cfg.session.last_will.topic          = s_lwtTopicBuf;
    mqtt_cfg.session.last_will.msg            = kOffline;
    mqtt_cfg.session.last_will.msg_len        = kOfflineLen;
    mqtt_cfg.session.last_will.qos            = 1;
    mqtt_cfg.session.last_will.retain         = true;
    mqtt_cfg.session.message_retransmit_timeout = 0;

    mqtt_cfg.network.disable_auto_reconnect = true;
    mqtt_cfg.network.reconnect_timeout_ms   = kReconnectTimeoutMs;
    mqtt_cfg.network.timeout_ms             = kWifiNetworkTimeoutMs;
    mqtt_cfg.network.tcp_keep_alive_cfg.keep_alive_enable  = true;
    mqtt_cfg.network.tcp_keep_alive_cfg.keep_alive_idle     = 60;
    mqtt_cfg.network.tcp_keep_alive_cfg.keep_alive_interval = 20;
    mqtt_cfg.network.tcp_keep_alive_cfg.keep_alive_count    = 3;

    mqtt_cfg.task.priority   = 5;
    mqtt_cfg.task.stack_size = kMqttClientTaskStackBytes;

    mqtt_cfg.buffer.size     = 512;
    mqtt_cfg.buffer.out_size = 512;
    mqtt_cfg.outbox.limit    = kMqttOutboxLimitBytes;

    ESP_LOGI(TAG, "MQTT client init TLS… server %s:%u id %s", cfg.server,
             static_cast<unsigned>(cfg.port), s_clientIdBuf);

    s_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_client == nullptr) {
        ESP_LOGE(TAG, "esp_mqtt_client_init failed");
        mqttClientUnlock();
        return false;
    }

    const esp_err_t regErr =
        esp_mqtt_client_register_event(s_client, MQTT_EVENT_ANY, mqttEventHandler, nullptr);
    if (regErr != ESP_OK) {
        ESP_LOGE(TAG, "esp_mqtt_client_register_event failed: %s", esp_err_to_name(regErr));
        mqttKillClientImpl();
        mqttClientUnlock();
        return false;
    }

    mqttClientUnlock();
    return true;
}

// Stop + destroy client; prefer mqttKillClient() (mutex). Clear s_client before stop (reentrancy).
static void mqttKillClientImpl() {
    if (s_client == nullptr) {
        return;
    }

    s_disconnectIntentional.store(true, std::memory_order_release);

    esp_mqtt_client_handle_t cli = s_client;
    s_client = nullptr;
    portENTER_CRITICAL(&s_mqttSubTopicMux);
    s_mqttSubTopicLen      = 0;
    s_mqttSubTopicCache[0] = '\0';
    portEXIT_CRITICAL(&s_mqttSubTopicMux);

    const esp_err_t st = esp_mqtt_client_stop(cli);
    if (st != ESP_OK && st != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "esp_mqtt_client_stop: %s", esp_err_to_name(st));
    }

    const esp_err_t de = esp_mqtt_client_destroy(cli);
    if (de != ESP_OK) {
        ESP_LOGW(TAG, "esp_mqtt_client_destroy: %s", esp_err_to_name(de));
    }

    s_connected.store(false, std::memory_order_release);
    s_connectPending.store(false, std::memory_order_release);
    s_disconnectIntentional.store(false, std::memory_order_release);
}

static void mqttKillClient() {
    mqttClientLock();
    mqttKillClientImpl();
    mqttClientUnlock();
}

static bool mqttPublishChayaLocked() {
    if (!s_connected.load(std::memory_order_acquire)) {
        ESP_LOGW(TAG, "Publish skipped: not connected");
        return false;
    }
    char topicPub[sizeof(MqttConfig::topicPub)]{};
    mqttCfgTopicPubLockedCopy(topicPub, sizeof(topicPub));

    char buf[16];
    const long nextVal = static_cast<long>(heartSentCounter.load(std::memory_order_relaxed)) + 1L;
    if (nextVal > INT_MAX) {
        ESP_LOGW(TAG, "Publish skipped: heartSentCounter at maximum");
        return false;
    }
    static_cast<void>(snprintf(buf, sizeof(buf), "%ld", nextVal));

    mqttClientLock();
    esp_mqtt_client_handle_t cli = s_client;
    if (cli == nullptr) {
        mqttClientUnlock();
        return false;
    }

    // QoS 0, retained; success if msg_id >= 0.
    const int pid = esp_mqtt_client_publish(cli, topicPub, buf, static_cast<int>(strlen(buf)), /*qos=*/0,
                                            /*retain=*/1);
    mqttClientUnlock();
    return pid >= 0;
}

bool mqttPublishChaya() {
    if (g_chayaPublishMutex == nullptr) {
        return false;
    }
    xSemaphoreTake(g_chayaPublishMutex, portMAX_DELAY);
    const bool ok = mqttPublishChayaLocked();
    xSemaphoreGive(g_chayaPublishMutex);
    return ok;
}

bool mqttPublishChayaAndApplySentCounters() {
    if (g_chayaPublishMutex == nullptr) {
        return false;
    }
    xSemaphoreTake(g_chayaPublishMutex, portMAX_DELAY);
    const bool ok = mqttPublishChayaLocked();
    if (!ok) {
        xSemaphoreGive(g_chayaPublishMutex);
        return false;
    }
    const int cur = heartSentCounter.load(std::memory_order_relaxed);
    if (cur < INT_MAX) {
        heartSentCounter.store(cur + 1, std::memory_order_relaxed);
    }
    maybeSaveHeartSentCounter();
    requestHeartRedraw();
    xSemaphoreGive(g_chayaPublishMutex);
    return true;
}

void mqttDisconnect() {
    mqttKillClient();
}

void mqttSetup() {
    mqttKillClient();
    portENTER_CRITICAL(&s_mqttBackoffMux);
    lastMqttAttemptAt    = 0;
    mqttBackoffMs        = 0;
    mqttCurrentBackoffMs = kMqttBackoffInitialMs;
    portEXIT_CRITICAL(&s_mqttBackoffMux);
}

bool mqttIsConnected() {
    return s_connected.load(std::memory_order_acquire);
}

static void mqttLoopApplyWifiPowerSaveOnConnectChange(bool connected, bool& wasConnected) {
    if (wasConnected && !connected) {
        wlanSetStaPowerSaveMqttActive(false);
    }
    if (connected && !wasConnected) {
        portENTER_CRITICAL(&s_mqttBackoffMux);
        lastMqttAttemptAt    = 0;
        mqttBackoffMs        = 0;
        mqttCurrentBackoffMs = kMqttBackoffInitialMs;
        portEXIT_CRITICAL(&s_mqttBackoffMux);
        wlanSetStaPowerSaveMqttActive(true);
    }
    wasConnected = connected;
}

static void mqttLoopTryReconnect(MqttConfig& loopCfg, unsigned long now) {
    const bool pending   = s_connectPending.load(std::memory_order_acquire);
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
        return;
    }

    const unsigned long deferMs = mqttConnectPrecheckDeferMs();
    if (deferMs != 0U) {
        portENTER_CRITICAL(&s_mqttBackoffMux);
        mqttBackoffMs = deferMs;
        portEXIT_CRITICAL(&s_mqttBackoffMux);
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

    mqttClientLock();
    s_connectPending.store(true, std::memory_order_release);
    const esp_err_t sr = s_client != nullptr ? esp_mqtt_client_start(s_client) : ESP_FAIL;
    mqttClientUnlock();

    if (sr != ESP_OK) {
        ESP_LOGE(TAG, "esp_mqtt_client_start failed: %s", esp_err_to_name(sr));
        s_connectPending.store(false, std::memory_order_release);
        applyDisconnectFailureBackoff(/*wifiSuspect=*/!wlanStaConnectedOk());
        mqttKillClient();
    }
}

void mqttLoop() {
    static MqttConfig s_loopCfg{};
    const bool connectedEarly = mqttIsConnected();
    // Refresh cfg snapshot when disconnected or web applied new broker.
    if (!connectedEarly || mqttCfgConsumeDirtySnapshotNeeded()) {
        mqttCfgSnapshot(&s_loopCfg);
    }

    if (s_loopCfg.server[0] == '\0') {
        mqttClientLock();
        const bool hasClient = s_client != nullptr;
        mqttClientUnlock();
        if (hasClient || s_connectPending.load(std::memory_order_acquire)) {
            mqttKillClient();
        }
        return;
    }

    const unsigned long now = millis();

    static bool wasConnected = false;
    const bool              connected = mqttIsConnected();
    mqttLoopApplyWifiPowerSaveOnConnectChange(connected, wasConnected);

    mqttLoopTryReconnect(s_loopCfg, now);
}

void mqttPostponeConnect(unsigned long delayMs) {
    portENTER_CRITICAL(&s_mqttBackoffMux);
    lastMqttAttemptAt = millis();
    mqttBackoffMs     = delayMs;
    portEXIT_CRITICAL(&s_mqttBackoffMux);
}
