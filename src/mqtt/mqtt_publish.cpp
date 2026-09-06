#include "mqtt.h"

#include "mqtt_internal.h"
#include "mqtt_publish_ack.h"

#include "async/event_types.h"
#include "async/system_lifecycle.h"
#include "async/task_handles.h"
#include "audio/audio.h"
#include "config.h"
#include "display/display.h"
#include "heart/counter.h"
#include "heart/counter_pure.h"
#include "led/led.h"
#include "wifi/wlan.h"

#include <Arduino.h>

#include <climits>
#include <cstdio>
#include <cstring>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <mqtt_client.h>

#include "util/log_tag.h"

DEFINE_LOG_TAG("MQTT");

static std::atomic<bool> s_mqttPublishBlocked{false};
static portMUX_TYPE s_publishAckMux = portMUX_INITIALIZER_UNLOCKED;
static MqttPublishAckState s_publishAckState{};
static std::atomic<unsigned long> s_publishAckStartedMs{0};
static std::atomic<bool> s_ackTimerArmed{false};

namespace {

enum class PublishAsyncState : uint8_t { Idle = 0, Pending = 1, Ok = 2, Fail = 3 };
std::atomic<uint8_t> s_publishAsync{static_cast<uint8_t>(PublishAsyncState::Idle)};

bool publishAckPending() {
    portENTER_CRITICAL(&s_publishAckMux);
    const bool pending = mqttPublishAckIsPending(s_publishAckState);
    portEXIT_CRITICAL(&s_publishAckMux);
    return pending;
}

void completePublishAsync(PublishAsyncState state) {
    uint8_t expected = static_cast<uint8_t>(PublishAsyncState::Pending);
    (void)s_publishAsync.compare_exchange_strong(expected, static_cast<uint8_t>(state), std::memory_order_acq_rel);
}

void failPendingPublishAck(uint32_t clientGeneration) {
    portENTER_CRITICAL(&s_publishAckMux);
    const bool failed = mqttPublishAckFail(&s_publishAckState, clientGeneration);
    portEXIT_CRITICAL(&s_publishAckMux);
    if (failed) {
        s_ackTimerArmed.store(false, std::memory_order_release);
        completePublishAsync(PublishAsyncState::Fail);
    }
}

} // namespace

void mqttHandlePublishedAck(int messageId, uint32_t clientGeneration) {
    portENTER_CRITICAL(&s_publishAckMux);
    const bool confirmed = mqttPublishAckConfirm(&s_publishAckState, messageId, clientGeneration);
    portEXIT_CRITICAL(&s_publishAckMux);
    if (!confirmed) {
        return;
    }

    s_ackTimerArmed.store(false, std::memory_order_release);
    heartSentCounterApplyAfterSuccessfulPublish();
    completePublishAsync(PublishAsyncState::Ok);
    audioRequest(AudioMsg::Kind::Tx);
    (void)displayRequest(DisplayMsg::Cmd::DrawHeart, DisplayRequestMode::Content);
}

void mqttAbortPendingPublish(uint32_t clientGeneration) { failPendingPublishAck(clientGeneration); }

void mqttAbortPendingPublish() {
    uint32_t generation = 0;
    bool ackPending = false;
    portENTER_CRITICAL(&s_publishAckMux);
    ackPending = mqttPublishAckIsPending(s_publishAckState);
    if (ackPending) {
        generation = s_publishAckState.clientGeneration;
    }
    portEXIT_CRITICAL(&s_publishAckMux);
    if (ackPending) {
        failPendingPublishAck(generation);
    }
    uint8_t expected = static_cast<uint8_t>(PublishAsyncState::Pending);
    (void)s_publishAsync.compare_exchange_strong(expected, static_cast<uint8_t>(PublishAsyncState::Fail),
                                                 std::memory_order_acq_rel);
}

void mqttServicePublishAckTimeout() {
    const bool pendingFast = publishAckPending();
    const bool armed = s_ackTimerArmed.load(std::memory_order_acquire);
    const unsigned long started = s_publishAckStartedMs.load(std::memory_order_acquire);
    if (!mqttPublishAckTimeoutDue(pendingFast, armed, started, millis(), kMqttPublishAckWaitMs)) {
        return;
    }

    uint32_t generation = 0;
    bool pending = false;
    portENTER_CRITICAL(&s_publishAckMux);
    pending = mqttPublishAckIsPending(s_publishAckState);
    if (pending) {
        generation = s_publishAckState.clientGeneration;
    }
    portEXIT_CRITICAL(&s_publishAckMux);
    if (!pending) {
        return;
    }
    ESP_LOGW(TAG, "QoS 1 PUBACK timeout (wait_ms=%lu)", static_cast<unsigned long>(kMqttPublishAckWaitMs));
    failPendingPublishAck(generation);
}

/** Start QoS-1 publish; does not block the network task on PUBACK (STAB-04 / PERF-01). */
static bool mqttPublishChayaLocked() {
    if (g_systemShutdownInProgress.load(std::memory_order_acquire)) {
        ESP_LOGW(TAG, "Publish skipped: shutdown in progress");
        return false;
    }
    if (s_mqttPublishBlocked.load(std::memory_order_acquire)) {
        ESP_LOGW(TAG, "Publish skipped: broker settings changing");
        return false;
    }
    if (!s_connected.load(std::memory_order_acquire)) {
        ESP_LOGW(TAG, "Publish skipped: not connected");
        return false;
    }
    if (publishAckPending()) {
        ESP_LOGW(TAG, "Publish skipped: previous QoS 1 acknowledgement pending");
        return false;
    }
    char topicPub[sizeof(MqttConfig::topicPub)]{};
    mqttCfgTopicPubLockedCopy(topicPub, sizeof(topicPub));

    char buf[16];
    const int cur = heartSentCounter.load(std::memory_order_relaxed);
    if (cur >= INT_MAX) {
        ESP_LOGW(TAG, "Publish skipped: heartSentCounter at maximum");
        return false;
    }
    const int nextVal = heartSentCounterNextPure(cur);
    static_cast<void>(snprintf(buf, sizeof(buf), "%d", nextVal));

    if (!mqttClientLockTimed()) {
        ESP_LOGW(TAG, "Publish skipped: mqtt client mutex timeout");
        return false;
    }
    esp_mqtt_client_handle_t cli = s_client;
    if (cli == nullptr) {
        mqttClientUnlock();
        ESP_LOGW(TAG, "Publish skipped: mqtt client null");
        return false;
    }

    const uint32_t clientGeneration = s_clientGeneration.load(std::memory_order_acquire);
    const int pid = esp_mqtt_client_publish(cli, topicPub, buf, static_cast<int>(strlen(buf)), 1, 1);
    bool published = false;
    if (pid >= 0) {
        portENTER_CRITICAL(&s_publishAckMux);
        published = mqttPublishAckBegin(&s_publishAckState, pid, clientGeneration, nextVal);
        portEXIT_CRITICAL(&s_publishAckMux);
    }
    mqttClientUnlock();
    if (pid < 0) {
        ESP_LOGW(TAG, "Publish failed: esp_mqtt_client_publish returned %d", pid);
        return false;
    }
    if (!published) {
        ESP_LOGW(TAG, "Publish skipped: could not begin PUBACK wait (msg_id=%d)", pid);
        return false;
    }
    s_publishAckStartedMs.store(millis(), std::memory_order_release);
    s_ackTimerArmed.store(true, std::memory_order_release);
    ESP_LOGD(TAG, "Published chaya QoS 1 → %s payload=%s (msg_id=%d)", topicPub, buf, pid);
    return true;
}

bool mqttPublishChayaAndApplySentCounters() {
    if (g_chayaPublishMutex == nullptr) {
        return false;
    }
    if (xSemaphoreTake(g_chayaPublishMutex, kChayaPublishLockTimeoutTicks) != pdTRUE) {
        ESP_LOGW(TAG, "Publish skipped: chaya mutex timeout");
        return false;
    }
    const bool ok = mqttPublishChayaLocked();
    xSemaphoreGive(g_chayaPublishMutex);
    return ok;
}

MqttChayaPublishAsync mqttRequestChayaPublishAsync() {
    if (g_systemShutdownInProgress.load(std::memory_order_acquire)) {
        mqttAbortPendingPublish();
        return MqttChayaPublishAsync::Fail;
    }
    uint8_t expected = static_cast<uint8_t>(PublishAsyncState::Idle);
    if (s_publishAsync.compare_exchange_strong(expected, static_cast<uint8_t>(PublishAsyncState::Pending),
                                               std::memory_order_acq_rel)) {
        if (!netCmdTrySend(NetCmd::ChayaPublish)) {
            ESP_LOGW(TAG, "ChayaPublish netCmd queue full — keeping pending for retry");
            return MqttChayaPublishAsync::Pending;
        }
    }
    return mqttPollChayaPublishAsync();
}

MqttChayaPublishAsync mqttPollChayaPublishAsync() {
    switch (static_cast<PublishAsyncState>(s_publishAsync.load(std::memory_order_acquire))) {
    case PublishAsyncState::Pending:
        return MqttChayaPublishAsync::Pending;
    case PublishAsyncState::Ok:
        return MqttChayaPublishAsync::Ok;
    case PublishAsyncState::Fail:
        return MqttChayaPublishAsync::Fail;
    case PublishAsyncState::Idle:
    default:
        return MqttChayaPublishAsync::Idle;
    }
}

void mqttRunChayaPublishOnNetworkTask() {
    if (g_systemShutdownInProgress.load(std::memory_order_acquire)) {
        mqttAbortPendingPublish();
        return;
    }
    if (s_publishAsync.load(std::memory_order_acquire) != static_cast<uint8_t>(PublishAsyncState::Pending)) {
        return;
    }
    if (publishAckPending()) {
        return;
    }
    // Start only — PUBACK / timeout complete Ok/Fail asynchronously (STAB-04 / PERF-01).
    if (!mqttPublishChayaAndApplySentCounters()) {
        s_publishAsync.store(static_cast<uint8_t>(PublishAsyncState::Fail), std::memory_order_release);
    }
}

bool mqttChayaPublishAsyncIsPending() {
    return s_publishAsync.load(std::memory_order_acquire) == static_cast<uint8_t>(PublishAsyncState::Pending);
}

void mqttClearChayaPublishAsync() {
    s_publishAsync.store(static_cast<uint8_t>(PublishAsyncState::Idle), std::memory_order_release);
}

bool mqttPublishBlocked() { return s_mqttPublishBlocked.load(std::memory_order_acquire); }

void mqttBeginSettingsApply() { s_mqttPublishBlocked.store(true, std::memory_order_release); }

void mqttEndSettingsApply() { s_mqttPublishBlocked.store(false, std::memory_order_release); }

ChayaSendResult chayaRequestSend() {
    if (g_systemShutdownInProgress.load(std::memory_order_acquire) || configIsApMode() || !mqttCfgIsHeartReady()) {
        return ChayaSendResult::Unavailable;
    }
    if (mqttPublishBlocked() || ledIsTxSendBusy()) {
        return ChayaSendResult::Busy;
    }
    if (!ledStartChayaSendSequence()) {
        return ChayaSendResult::Busy;
    }
    return ChayaSendResult::Started;
}
