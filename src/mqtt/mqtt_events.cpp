#include "mqtt_internal.h"

#include "backoff.h"
#include "counter_payload.h"

#include "async/event_types.h"
#include "async/task_handles.h"
#include "config.h"
#include "audio/audio.h"
#include "display/display.h"
#include "heart/counter.h"
#include "led/led.h"
#include "led/led_config.h"
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

unsigned long lastMqttAttemptAt    = 0;
unsigned long mqttBackoffMs        = 0;
unsigned long mqttCurrentBackoffMs = kMqttBackoffInitialMs;
portMUX_TYPE  s_mqttBackoffMux     = portMUX_INITIALIZER_UNLOCKED;

static char     s_fragAccBuf[16];
static uint32_t s_fragExpectTotal = 0;
static unsigned s_fragHave        = 0;
static portMUX_TYPE s_mqttFragmentMux = portMUX_INITIALIZER_UNLOCKED;

void mqttResetFragmentState() {
    portENTER_CRITICAL(&s_mqttFragmentMux);
    s_fragExpectTotal = 0;
    s_fragHave        = 0;
    portEXIT_CRITICAL(&s_mqttFragmentMux);
}

static void mqttQueueKillClientFromEvent() {
    if (netCmdTrySend(NetCmd::MqttKillClient)) {
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
    audioRequest(AudioMsg::Kind::Rx);
    ledRefreshPulseBegin();

    // Display layer owns the 30 s leading/trailing coalesce; always report the change.
    if (!displayRequest(DisplayMsg::Cmd::DrawHeart, DisplayRequestMode::Content, 0U)) {
        ledRefreshPulseEndAfter(kLedRefreshAckMs);
    }
}

static bool feedFragmentedPayload(esp_mqtt_event_handle_t ev) {
    if (ev == nullptr || ev->data == nullptr || ev->data_len <= 0) {
        return false;
    }

    char     completePayload[sizeof(s_fragAccBuf)]{};
    uint32_t completeLength = 0;
    bool     rejected       = false;
    bool     partial        = false;
    [[maybe_unused]] unsigned partialHave  = 0;
    [[maybe_unused]] uint32_t partialTotal = 0;

    portENTER_CRITICAL(&s_mqttFragmentMux);
    if (ev->topic != nullptr && ev->topic_len > 0 && ev->current_data_offset == 0) {
        s_fragHave        = 0;
        s_fragExpectTotal = static_cast<uint32_t>(ev->total_data_len);
        const uint32_t kMaxStored = sizeof(s_fragAccBuf) - 1U;
        if (s_fragExpectTotal == 0U || s_fragExpectTotal > kMaxStored) {
            s_fragExpectTotal = 0;
            rejected          = true;
        }
    }

    if (!rejected && s_fragExpectTotal > 0U) {
        const unsigned add = static_cast<unsigned>(ev->data_len);
        const bool offsetMatches =
            static_cast<uint32_t>(ev->current_data_offset) == static_cast<uint32_t>(s_fragHave);
        if (!offsetMatches || s_fragHave > s_fragExpectTotal
            || add > s_fragExpectTotal - s_fragHave) {
            s_fragExpectTotal = 0;
            s_fragHave        = 0;
            rejected          = true;
        } else {
            memcpy(s_fragAccBuf + s_fragHave, ev->data, add);
            s_fragHave += add;
            if (s_fragHave < s_fragExpectTotal) {
                partial      = true;
                partialHave  = s_fragHave;
                partialTotal = s_fragExpectTotal;
            } else {
                completeLength = s_fragExpectTotal;
                memcpy(completePayload, s_fragAccBuf, completeLength);
                s_fragExpectTotal = 0;
                s_fragHave        = 0;
            }
        }
    }
    portEXIT_CRITICAL(&s_mqttFragmentMux);

    if (rejected) {
        ESP_LOGD(TAG, "Ignoring malformed MQTT fragment");
        return false;
    }
    if (partial) {
        ESP_LOGV(TAG, "MQTT fragment: partial %u/%" PRIu32 " bytes", partialHave, partialTotal);
        return false;
    }
    if (completeLength == 0U) {
        return false;
    }
    ESP_LOGV(TAG, "MQTT fragment: complete %" PRIu32 " bytes", completeLength);
    handleCounterPayload(completePayload, completeLength);
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

template <typename Fn>
static bool mqttWithLiveClient(esp_mqtt_client_handle_t cli, uint32_t generation, Fn&& fn) {
    if (!mqttClientLockTimed()) {
        return false;
    }
    const bool live = (s_client == cli
                       && s_clientGeneration.load(std::memory_order_acquire) == generation);
    if (live) {
        fn(cli);
    }
    mqttClientUnlock();
    return live;
}

template <typename Fn>
static int mqttWithLiveClientInt(esp_mqtt_client_handle_t cli, uint32_t generation, Fn&& fn) {
    if (!mqttClientLockTimed()) {
        return -1;
    }
    const bool live = (s_client == cli
                       && s_clientGeneration.load(std::memory_order_acquire) == generation);
    const int result = live ? fn(cli) : -1;
    mqttClientUnlock();
    return result;
}

static bool mqttEventDisconnectIfLive(esp_mqtt_client_handle_t cli, uint32_t generation) {
    return mqttWithLiveClient(cli, generation, [](esp_mqtt_client_handle_t c) {
        (void)esp_mqtt_client_disconnect(c);
    });
}

static int mqttEventSubscribeIfLive(esp_mqtt_client_handle_t cli, uint32_t generation,
                                    const char* topic, int qos) {
    return mqttWithLiveClientInt(cli, generation, [&](esp_mqtt_client_handle_t c) {
        return esp_mqtt_client_subscribe(c, topic, qos);
    });
}

static int mqttEventPublishIfLive(esp_mqtt_client_handle_t cli, uint32_t generation,
                                  const char* topic, const char* payload, int length, int qos,
                                  int retain) {
    return mqttWithLiveClientInt(cli, generation, [&](esp_mqtt_client_handle_t c) {
        return esp_mqtt_client_publish(c, topic, payload, length, qos, retain);
    });
}

static bool mqttEventMarkConnectedIfLive(esp_mqtt_client_handle_t cli, uint32_t generation) {
    return mqttWithLiveClient(cli, generation, [](esp_mqtt_client_handle_t) {
        s_connected.store(true, std::memory_order_release);
    });
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
            ESP_LOGW(TAG, "MQTT connected: cfg snapshot timeout — disconnecting");
            (void)mqttEventDisconnectIfLive(ev->client, handlerGeneration);
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
            const int mid =
                mqttEventSubscribeIfLive(ev->client, handlerGeneration, cfg.topicSub, 1);
            if (mid < 0) {
                ESP_LOGE(TAG, "MQTT subscribe failed — disconnecting for retry");
                (void)mqttEventDisconnectIfLive(ev->client, handlerGeneration);
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
        if (mqttEventPublishIfLive(ev->client, handlerGeneration, lwtPublishTopic, kOnline,
                                   kOnlineLen, 1, 1)
            < 0) {
            ESP_LOGW(TAG, "MQTT publish retained online failed");
            break;
        }

        (void)mqttEventMarkConnectedIfLive(ev->client, handlerGeneration);
        ledPlayPreset(LedPreset::MqttUp);
        break;
    }
    case MQTT_EVENT_DATA:
        if (!mqttEventGenerationStillValid(handlerGeneration)) {
            mqttResetFragmentState();
            break;
        }
        if (ev->topic == nullptr || ev->topic_len <= 0) {
            feedFragmentedPayload(ev);
        } else if (topicMatchesSubscribe(ev)) {
            feedFragmentedPayload(ev);
        } else {
            ESP_LOGD(TAG, "Ignoring MQTT payload (wrong topic)");
            mqttResetFragmentState();
        }
        break;

    case MQTT_EVENT_PUBLISHED:
        if (mqttEventGenerationStillValid(handlerGeneration)) {
            ESP_LOGD(TAG, "PUBACK msg_id=%d gen=%u", ev->msg_id,
                     static_cast<unsigned>(handlerGeneration));
            mqttHandlePublishedAck(ev->msg_id, handlerGeneration);
        }
        break;

    case MQTT_EVENT_DISCONNECTED: {
        if (!mqttEventGenerationStillValid(handlerGeneration)) {
            break;
        }
        mqttAbortPendingPublish(handlerGeneration);
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
        if (ev->error_handle != nullptr) {
            const esp_mqtt_error_codes_t* eh = ev->error_handle;
            ESP_LOGW(TAG,
                     "MQTT EVENT_ERROR: error_type=%d connect_rc=%d tls=%s sock_errno=%d",
                     static_cast<int>(eh->error_type),
                     static_cast<int>(eh->connect_return_code),
                     esp_err_to_name(eh->esp_tls_last_esp_err), eh->esp_transport_sock_errno);
            if (eh->error_type == MQTT_ERROR_TYPE_SUBSCRIBE_FAILED) {
                ESP_LOGE(TAG, "MQTT subscribe rejected by broker");
                (void)mqttEventDisconnectIfLive(ev->client, handlerGeneration);
            }
        } else {
            ESP_LOGW(TAG, "MQTT EVENT_ERROR (no error_handle)");
        }
        break;

    default:
        break;
    }
}
