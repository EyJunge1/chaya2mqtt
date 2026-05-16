#include <Arduino.h>

#include "mqtt.h"

#include "config.h"
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
#include <mqtt_client.h>

#include "log_tag.h"

DEFINE_LOG_TAG("MQTT");

/** When NVS has no broker configured, avoid tight reconnect attempts. */
static constexpr unsigned long kMqttBrokerMissingBackoffMs     = 60000UL;
static constexpr unsigned long kMqttBackoffInitialMs           = 30000UL;
static constexpr unsigned long kMqttBackoffMaxMs               = 60000UL;
/** Longer wait when STA is down so reconnect/logging is not spammed. */
static constexpr unsigned long kMqttWifiDownBackoffMs       = 20000UL;
/** After TLS failure while STA dropped (BEACON_TIMEOUT etc.), wait longer before retry. */
static constexpr unsigned long kMqttWifiLostDuringTlsBackoffMs = 90000UL;
/** SNTP must be valid before MQTT/TLS (mbedTLS certificate notBefore/notAfter checks). */
static constexpr unsigned long kMqttNtpRetryMs = 2000UL;

/** Must destroy/restart MQTT client only from the Arduino loop task — not inside mqtt_event_handler */
static std::atomic<bool> s_needMqttKillFromLoop{false};

static esp_mqtt_client_handle_t s_client                      = nullptr;
static std::atomic<bool>       s_connected{false};
/** True between esp_mqtt_client_start() and CONNECTED / failure DISCONNECTED teardown. */
static std::atomic<bool>       s_connectPending{false};
/** DISCONNECTED backoff skipped when tearing down voluntarily (mqttKillClient). */
static std::atomic<bool>       s_disconnectIntentional{false};

static unsigned long lastMqttAttemptAt   = 0;
static unsigned long mqttBackoffMs       = 0;
static unsigned long mqttCurrentBackoffMs = kMqttBackoffInitialMs;

static char s_clientIdBuf[24]{};
/** LWT + retained online topic `{topic_pub}/lwt`; lives for client lifetime */
static char s_lwtTopicBuf[sizeof(MqttConfig::topicPub) + 16U]{};

static void mqttKillClient();

static void applyDisconnectFailureBackoff(bool wifiSuspectDuringFailure) {
    unsigned long waitMs = mqttCurrentBackoffMs;
    if (wifiSuspectDuringFailure || !wlanStaConnectedOk()) {
        waitMs = std::max(waitMs, kMqttWifiLostDuringTlsBackoffMs);
        ESP_LOGW(TAG, "Wi-Fi not healthy after MQTT disconnect — backing off %lu s", waitMs / 1000UL);
    }
    mqttCurrentBackoffMs = std::min(mqttCurrentBackoffMs * 2UL, kMqttBackoffMaxMs);
    mqttBackoffMs        = waitMs;
    lastMqttAttemptAt    = millis();
    ESP_LOGI(TAG, "Next connect attempt in %lu s", waitMs / 1000UL);
}

/** Return nonzero ms to defer connect; **0** = prerequisites satisfied. */
static unsigned long mqttConnectPrecheckDeferMs() {
    MqttConfig cfg{};
    mqttCfgSnapshot(&cfg);

    if (strlen(cfg.server) == 0U) {
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

static void handleCounterPayload(char* payload, unsigned int length) {
    if (length == 0 || length > 10U) {
        ESP_LOGD(TAG, "Invalid counter payload length %u", length);
        return;
    }

    payload[length] = '\0';

    char*      endPtr  = nullptr;
    errno              = 0;
    const long parsed  = strtol(payload, &endPtr, 10);
    if (errno == ERANGE || endPtr != payload + length || parsed < 0
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

/** esp_mqtt can split payloads across DATA events — reassemble tiny counter messages */
static bool feedFragmentedPayload(esp_mqtt_event_handle_t ev) {
    static char      accBuf[16];
    static uint32_t  expectTotal = 0;
    static unsigned  have       = 0;

    if (ev == nullptr || ev->data == nullptr || ev->data_len <= 0) {
        return false;
    }

    /* First fragment carries topic metadata */
    if (ev->topic != nullptr && ev->topic_len > 0 && ev->current_data_offset == 0) {
        have           = 0;
        expectTotal    = static_cast<uint32_t>(ev->total_data_len);
        const uint32_t kMaxStored = sizeof(accBuf) - 1U;
        if (expectTotal == 0U || expectTotal > kMaxStored) {
            ESP_LOGD(TAG, "Ignoring MQTT fragment (unexpected total_len=%" PRIu32 ")", expectTotal);
            expectTotal = 0;
            have       = 0;
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
    MqttConfig cfg{};
    mqttCfgSnapshot(&cfg);
    const size_t expected = strlen(cfg.topicSub);
    if (expected != static_cast<size_t>(ev->topic_len)) {
        return false;
    }
    return memcmp(ev->topic, cfg.topicSub, expected) == 0;
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
        static_cast<void>(snprintf(s_lwtTopicBuf, sizeof(s_lwtTopicBuf), "%s/lwt", cfg.topicPub));

        mqttCurrentBackoffMs = kMqttBackoffInitialMs;

        ESP_LOGI(TAG, "MQTT connected; subscribing (QoS 1): %s", cfg.topicSub);
        const int mid = esp_mqtt_client_subscribe(ev->client, cfg.topicSub, 1);
        if (mid < 0) {
            ESP_LOGE(TAG, "MQTT subscribe failed — disconnecting for retry");
            (void)esp_mqtt_client_disconnect(ev->client);
            break;
        }

        constexpr const char kOnline[] = "online";
        if (esp_mqtt_client_publish(ev->client, s_lwtTopicBuf, kOnline, static_cast<int>(strlen(kOnline)), 0, 1)
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
        /* mqttKillClient() runs esp_mqtt_client_stop synchronously; disconnect was intentional — skip backoff. */
        const bool intentional = s_disconnectIntentional.load(std::memory_order_acquire);
        if (!intentional) {
            applyDisconnectFailureBackoff(/*wifiSuspect=*/!wlanStaConnectedOk());
            s_needMqttKillFromLoop.store(true, std::memory_order_release);
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
    if (s_client != nullptr) {
        return true;
    }

    MqttConfig cfg{};
    mqttCfgSnapshot(&cfg);

    snprintf(s_clientIdBuf, sizeof(s_clientIdBuf), "Chaya2MQTT-%04lX",
             static_cast<unsigned long>(esp_random() & 0xffffU));

    snprintf(s_lwtTopicBuf, sizeof(s_lwtTopicBuf), "%s/lwt", cfg.topicPub);

    if (installCaBundleLocked() != ESP_OK) {
        ESP_LOGE(TAG, "esp_crt_bundle_set failed");
        return false;
    }

    const char* mqttUser = (cfg.username[0] != '\0') ? static_cast<const char*>(cfg.username) : nullptr;

    constexpr const char kOffline[] = "offline";
    constexpr int        kWifiNetworkTimeoutMs = 5000;
    constexpr int        kReconnectTimeoutMs = 60000;

    esp_mqtt_client_config_t mqtt_cfg{};
    mqtt_cfg.broker.address.hostname             = cfg.server;
    mqtt_cfg.broker.address.port                 = cfg.port;
    mqtt_cfg.broker.address.transport           = MQTT_TRANSPORT_OVER_SSL;
    mqtt_cfg.broker.address.uri                 = nullptr;
    mqtt_cfg.broker.address.path                = nullptr;

    mqtt_cfg.broker.verification.use_global_ca_store     = false;
    mqtt_cfg.broker.verification.crt_bundle_attach       = esp_crt_bundle_attach;
    mqtt_cfg.broker.verification.certificate            = nullptr;
    mqtt_cfg.broker.verification.certificate_len       = 0;
    mqtt_cfg.broker.verification.skip_cert_common_name_check = false;

    mqtt_cfg.credentials.username     = mqttUser;
    mqtt_cfg.credentials.client_id    = s_clientIdBuf;
    mqtt_cfg.credentials.set_null_client_id = false;
    mqtt_cfg.credentials.authentication.password =
        mqttUser != nullptr ? static_cast<const char*>(cfg.password) : nullptr;

    mqtt_cfg.session.disable_clean_session     = false;
    mqtt_cfg.session.keepalive               = 60;
    mqtt_cfg.session.disable_keepalive       = false;
    mqtt_cfg.session.protocol_ver           = MQTT_PROTOCOL_UNDEFINED;
    mqtt_cfg.session.last_will.topic       = s_lwtTopicBuf;
    mqtt_cfg.session.last_will.msg         = kOffline;
    mqtt_cfg.session.last_will.msg_len     = static_cast<int>(strlen(kOffline));
    mqtt_cfg.session.last_will.qos         = 1;
    mqtt_cfg.session.last_will.retain      = true;
    mqtt_cfg.session.message_retransmit_timeout = 0;

    mqtt_cfg.network.disable_auto_reconnect = true;
    mqtt_cfg.network.reconnect_timeout_ms   = kReconnectTimeoutMs;
    mqtt_cfg.network.timeout_ms           = kWifiNetworkTimeoutMs;
    mqtt_cfg.network.tcp_keep_alive_cfg.keep_alive_enable  = true;
    mqtt_cfg.network.tcp_keep_alive_cfg.keep_alive_idle     = 60;
    mqtt_cfg.network.tcp_keep_alive_cfg.keep_alive_interval = 20;
    mqtt_cfg.network.tcp_keep_alive_cfg.keep_alive_count      = 3;

    mqtt_cfg.task.priority               = 5;
    mqtt_cfg.task.stack_size             = 10240;

    mqtt_cfg.buffer.size               = 512;
    mqtt_cfg.buffer.out_size           = 512;
    mqtt_cfg.outbox.limit              = 0;

    ESP_LOGI(TAG, "MQTT client init TLS… server %s:%u id %s", cfg.server,
             static_cast<unsigned>(cfg.port), s_clientIdBuf);

    s_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_client == nullptr) {
        ESP_LOGE(TAG, "esp_mqtt_client_init failed");
        return false;
    }

    const esp_err_t regErr = esp_mqtt_client_register_event(s_client, MQTT_EVENT_ANY, mqttEventHandler, nullptr);
    if (regErr != ESP_OK) {
        ESP_LOGE(TAG, "esp_mqtt_client_register_event failed: %s", esp_err_to_name(regErr));
        mqttKillClient();
        return false;
    }

    return true;
}

/** Stop and destroy MQTT client (Arduino loop / setup only). */
static void mqttKillClient() {
    if (s_client == nullptr) {
        s_needMqttKillFromLoop.store(false, std::memory_order_release);
        return;
    }

    s_disconnectIntentional.store(true, std::memory_order_release);

    esp_mqtt_client_handle_t cli = s_client;
    /* Clear handle before synchronous stop avoids re-entrancy if events post during stop */
    s_client = nullptr;

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
    s_needMqttKillFromLoop.store(false, std::memory_order_release);
}

static void mqttProcessDeferredKillLocked() {
    if (!s_needMqttKillFromLoop.exchange(false, std::memory_order_acq_rel)) {
        return;
    }
    mqttKillClient();
}

bool mqttPublishChaya() {
    if (!s_connected.load(std::memory_order_acquire)) {
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

    esp_mqtt_client_handle_t cli = s_client;
    if (cli == nullptr) {
        return false;
    }

    /* Match PubSubClient: QoS 0 publish, retained payload (success: msg_id >= 0) */
    const int pid = esp_mqtt_client_publish(cli, cfg.topicPub, buf, static_cast<int>(strlen(buf)), /*qos=*/0,
                                            /*retain=*/1);
    return pid >= 0;}

void mqttDisconnect() {
    mqttKillClient();
}

void mqttSetup() {
    mqttKillClient();
    lastMqttAttemptAt    = 0;
    mqttBackoffMs        = 0;
    mqttCurrentBackoffMs = kMqttBackoffInitialMs;
}

bool mqttIsConnected() {
    return s_connected.load(std::memory_order_acquire);
}

void mqttLoop() {
    static MqttConfig s_loopCfg{};
    const bool connectedEarly = mqttIsConnected();
    if (!connectedEarly || mqttCfgConsumeDirtySnapshotNeeded()) {
        mqttCfgSnapshot(&s_loopCfg);
    }

    mqttProcessDeferredKillLocked();

    if (s_loopCfg.server[0] == '\0') {
        if (s_client != nullptr || s_connectPending.load(std::memory_order_acquire)) {
            mqttKillClient();
        }
        return;
    }

    const unsigned long now = millis();

    static bool wasConnected = false;
    const bool connected   = mqttIsConnected();
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

    /* Try (re-)connect — single flight while pending */
    const bool pending = s_connectPending.load(std::memory_order_acquire);
    if (!connected && !pending) {
        /* Unsigned subtraction wraps safely across millis() overflow */
        const bool backoffElapsed =
            mqttBackoffMs == 0UL || ((now - lastMqttAttemptAt) >= mqttBackoffMs);

        if (backoffElapsed) {
            lastMqttAttemptAt = now;
            const unsigned long deferMs = mqttConnectPrecheckDeferMs();
            if (deferMs != 0U) {
                mqttBackoffMs = deferMs;
            } else {
                mqttBackoffMs = 0;
                ESP_LOGI(TAG, "MQTT start… server %s:%u", s_loopCfg.server,
                         static_cast<unsigned>(s_loopCfg.port));

                if (!mqttEnsureClientAllocated()) {
                    applyDisconnectFailureBackoff(/*wifiSuspect=*/!wlanStaConnectedOk());
                    return;
                }

                s_connectPending.store(true, std::memory_order_release);
                const esp_err_t sr = esp_mqtt_client_start(s_client);
                if (sr != ESP_OK) {
                    ESP_LOGE(TAG, "esp_mqtt_client_start failed: %s", esp_err_to_name(sr));
                    s_connectPending.store(false, std::memory_order_release);
                    applyDisconnectFailureBackoff(/*wifiSuspect=*/!wlanStaConnectedOk());
                    mqttKillClient();
                }
            }
        }
    } else if (connected) {
        /* esp_mqtt internal task maintains the session — no mqtt_loop() pumping */
    }

    mqttProcessDeferredKillLocked();
}

void mqttPostponeConnect(unsigned long delayMs) {
    lastMqttAttemptAt = millis();
    mqttBackoffMs     = delayMs;
}
