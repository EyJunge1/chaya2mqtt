#include "mqtt_internal.h"

#include "backoff.h"
#include "counter_payload.h"

#include "async/event_types.h"
#include "async/task_handles.h"
#include "config.h"
#include "display/display.h"
#include "heart/counter.h"
#include "wifi/wlan.h"

#include <Arduino.h>

#include <cinttypes>
#include <cstdio>
#include <cstring>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "util/log_tag.h"

DEFINE_LOG_TAG("MQTT");

std::atomic<unsigned long> s_lastBrokerRedrawMs{0};

unsigned long lastMqttAttemptAt    = 0;
unsigned long mqttBackoffMs        = 0;
unsigned long mqttCurrentBackoffMs = kMqttBackoffInitialMs;
portMUX_TYPE  s_mqttBackoffMux     = portMUX_INITIALIZER_UNLOCKED;

static char     s_fragAccBuf[16];
static uint32_t s_fragExpectTotal = 0;
static unsigned s_fragHave        = 0;

void mqttResetFragmentState() {
    s_fragExpectTotal = 0;
    s_fragHave        = 0;
}

static void mqttQueueKillClientFromEvent() {
    if (g_netCmdQueue == nullptr) {
        return;
    }
    const NetCmd cmd = NetCmd::MqttKillClient;
    if (xQueueSend(g_netCmdQueue, &cmd, 0) == pdTRUE) {
        return;
    }
    s_mqttKillCoalesce.store(true, std::memory_order_release);
    ESP_LOGW(TAG, "netCmd queue full (MqttKillClient) — coalesced");
}

void applyDisconnectFailureBackoff(bool wifiSuspectDuringFailure) {
    MqttBackoffState st{};
    portENTER_CRITICAL(&s_mqttBackoffMux);
    st.currentBackoffMs = mqttCurrentBackoffMs;
    portEXIT_CRITICAL(&s_mqttBackoffMux);

    const bool wifiSuspect = wifiSuspectDuringFailure || !wlanStaConnectedOk();
    const unsigned long waitMs = mqttNextFailureBackoffMs(st, wifiSuspect);
    if (wifiSuspect) {
        ESP_LOGW(TAG, "Wi-Fi not healthy after MQTT disconnect — backing off %lu s", waitMs / 1000UL);
    }
    portENTER_CRITICAL(&s_mqttBackoffMux);
    mqttCurrentBackoffMs = st.currentBackoffMs;
    mqttBackoffMs        = waitMs;
    lastMqttAttemptAt    = millis();
    portEXIT_CRITICAL(&s_mqttBackoffMux);
    ESP_LOGI(TAG, "Next connect attempt in %lu s", waitMs / 1000UL);
}

static void handleCounterPayload(const char* payload, unsigned int length) {
    long parsed = 0;
    if (!mqttParseCounterPayload(payload, length, &parsed)) {
        ESP_LOGD(TAG, "Invalid counter payload (len=%u)", length);
        return;
    }

    const int newCounter = static_cast<int>(parsed);
    if (newCounter == heartCounter.load(std::memory_order_relaxed)) {
        return;
    }

    ESP_LOGI(TAG, "Heart counter from MQTT (remote): %d", newCounter);
    heartCounterStoreFromRemote(newCounter);

    const unsigned long nowMs  = millis();
    const unsigned long lastMs = s_lastBrokerRedrawMs.load(std::memory_order_relaxed);
    if (lastMs != 0UL && (nowMs - lastMs) < kMqttBrokerRedrawMinIntervalMs) {
        return;
    }
    s_lastBrokerRedrawMs.store(nowMs, std::memory_order_relaxed);
    requestHeartRedrawNonBlocking();
}

static bool feedFragmentedPayload(esp_mqtt_event_handle_t ev) {
    if (ev == nullptr || ev->data == nullptr || ev->data_len <= 0) {
        return false;
    }

    if (ev->topic != nullptr && ev->topic_len > 0 && ev->current_data_offset == 0) {
        s_fragHave        = 0;
        s_fragExpectTotal = static_cast<uint32_t>(ev->total_data_len);
        const uint32_t kMaxStored = sizeof(s_fragAccBuf) - 1U;
        if (s_fragExpectTotal == 0U || s_fragExpectTotal > kMaxStored) {
            ESP_LOGD(TAG, "Ignoring MQTT fragment (unexpected total_len=%" PRIu32 ")",
                     s_fragExpectTotal);
            mqttResetFragmentState();
            return false;
        }
        ESP_LOGV(TAG, "MQTT fragment: new stream total_len=%" PRIu32, s_fragExpectTotal);
    }

    if (s_fragExpectTotal == 0U) {
        return false;
    }

    const unsigned add = static_cast<unsigned>(ev->data_len);
    if (s_fragHave + add > sizeof(s_fragAccBuf) - 1U) {
        mqttResetFragmentState();
        return false;
    }
    memcpy(s_fragAccBuf + s_fragHave, ev->data, add);
    s_fragHave += add;

    if (s_fragHave < s_fragExpectTotal) {
        ESP_LOGV(TAG, "MQTT fragment: partial %u/%" PRIu32 " bytes", s_fragHave, s_fragExpectTotal);
        return false;
    }

    ESP_LOGV(TAG, "MQTT fragment: complete %" PRIu32 " bytes", s_fragExpectTotal);
    handleCounterPayload(s_fragAccBuf, s_fragExpectTotal);
    mqttResetFragmentState();
    return true;
}

static bool topicMatchesSubscribe(const esp_mqtt_event_handle_t ev) {
    if (ev->topic == nullptr || ev->topic_len <= 0) {
        return false;
    }
    portENTER_CRITICAL(&s_mqttSubTopicMux);
    const size_t cachedLen = s_mqttSubTopicLen;
    const bool   match     = (cachedLen > 0U) && (static_cast<size_t>(ev->topic_len) == cachedLen)
                         && (memcmp(ev->topic, s_mqttSubTopicCache, cachedLen) == 0);
    portEXIT_CRITICAL(&s_mqttSubTopicMux);
    return match;
}

static bool mqttEventClientStillLive(esp_mqtt_client_handle_t cli, uint32_t* outGeneration) {
    if (cli == nullptr) {
        return false;
    }
    if (!mqttClientLockTimed()) {
        return false;
    }
    const bool live = (s_client != nullptr && cli == s_client);
    if (live && outGeneration != nullptr) {
        *outGeneration = s_clientGeneration.load(std::memory_order_acquire);
    }
    mqttClientUnlock();
    return live;
}

static bool mqttEventGenerationStillValid(uint32_t generation) {
    if (!mqttClientLockTimed()) {
        return false;
    }
    const bool valid = (s_client != nullptr
                        && s_clientGeneration.load(std::memory_order_acquire) == generation);
    mqttClientUnlock();
    return valid;
}

void mqttEventHandler(void* /*handler_args*/, esp_event_base_t /*base*/, int32_t event_id,
                      void* event_data) {
    const esp_mqtt_event_handle_t ev = static_cast<esp_mqtt_event_handle_t>(event_data);
    if (ev == nullptr) {
        return;
    }
    uint32_t handlerGeneration = 0;
    if (!mqttEventClientStillLive(ev->client, &handlerGeneration)) {
        return;
    }

    switch (static_cast<esp_mqtt_event_id_t>(event_id)) {
    case MQTT_EVENT_CONNECTED: {
        if (!mqttEventGenerationStillValid(handlerGeneration)) {
            break;
        }
        s_connectPending.store(false, std::memory_order_release);

        MqttConfig cfg{};
        if (!mqttCfgSnapshotTimed(&cfg, 2000U)) {
            ESP_LOGW(TAG, "MQTT connected: cfg snapshot timeout — skipping subscribe setup");
            break;
        }
        char lwtPublishTopic[sizeof(s_lwtTopicBuf)];
        static_cast<void>(snprintf(lwtPublishTopic, sizeof(lwtPublishTopic), "%s/lwt", cfg.topicPub));

        portENTER_CRITICAL(&s_mqttBackoffMux);
        mqttCurrentBackoffMs = kMqttBackoffInitialMs;
        portEXIT_CRITICAL(&s_mqttBackoffMux);

        const bool shouldSubscribe =
            cfg.partnerDeviceId[0] != '\0' && mqttTopicSyntaxOk(cfg.topicSub, sizeof(cfg.topicSub));
        if (shouldSubscribe) {
            portENTER_CRITICAL(&s_mqttSubTopicMux);
            strlcpy(s_mqttSubTopicCache, cfg.topicSub, sizeof(s_mqttSubTopicCache));
            s_mqttSubTopicLen = strlen(s_mqttSubTopicCache);
            portEXIT_CRITICAL(&s_mqttSubTopicMux);

            ESP_LOGI(TAG, "MQTT connected; subscribing (QoS 1): %s", cfg.topicSub);
            const int mid = esp_mqtt_client_subscribe(ev->client, cfg.topicSub, 1);
            if (mid < 0) {
                ESP_LOGE(TAG, "MQTT subscribe failed — disconnecting for retry");
                (void)esp_mqtt_client_disconnect(ev->client);
                break;
            }
        } else {
            portENTER_CRITICAL(&s_mqttSubTopicMux);
            s_mqttSubTopicCache[0] = '\0';
            s_mqttSubTopicLen      = 0U;
            portEXIT_CRITICAL(&s_mqttSubTopicMux);
            ESP_LOGI(TAG, "MQTT connected; no partner — skipping subscribe");
        }

        constexpr const char kOnline[]   = "online";
        constexpr int        kOnlineLen  = sizeof(kOnline) - 1;
        if (esp_mqtt_client_publish(ev->client, lwtPublishTopic, kOnline, kOnlineLen, 1, 1) < 0) {
            ESP_LOGW(TAG, "MQTT publish retained online failed");
        }

        s_connected.store(true, std::memory_order_release);
        break;
    }
    case MQTT_EVENT_DATA:
        if (!mqttEventGenerationStillValid(handlerGeneration)) {
            mqttResetFragmentState();
            break;
        }
        if ((ev->topic == nullptr || ev->topic_len <= 0) && s_fragExpectTotal > 0U) {
            feedFragmentedPayload(ev);
        } else if (topicMatchesSubscribe(ev)) {
            feedFragmentedPayload(ev);
        } else {
            ESP_LOGD(TAG, "Ignoring MQTT payload (wrong topic)");
            mqttResetFragmentState();
        }
        break;

    case MQTT_EVENT_DISCONNECTED: {
        if (!mqttEventGenerationStillValid(handlerGeneration)) {
            break;
        }
        s_connected.store(false, std::memory_order_release);
        s_connectPending.store(false, std::memory_order_release);
        mqttResetFragmentState();
        const bool intentional = s_disconnectIntentional.load(std::memory_order_acquire);
        if (intentional) {
            ESP_LOGI(TAG, "MQTT disconnected (intentional teardown)");
            s_disconnectIntentional.store(false, std::memory_order_release);
        } else {
            const esp_mqtt_error_codes_t* eh = ev->error_handle;
            if (eh != nullptr) {
                ESP_LOGW(TAG,
                         "MQTT disconnected: error_type=%d connect_rc=%d tls=%s sock_errno=%d",
                         static_cast<int>(eh->error_type),
                         static_cast<int>(eh->connect_return_code),
                         esp_err_to_name(eh->esp_tls_last_esp_err), eh->esp_transport_sock_errno);
            } else {
                ESP_LOGW(TAG, "MQTT disconnected");
            }
        }
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
