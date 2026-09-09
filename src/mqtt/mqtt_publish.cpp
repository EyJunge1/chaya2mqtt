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

bool publishAckBlocksNewPublish() {
    portENTER_CRITICAL(&s_publishAckMux);
    const bool blocked = mqttPublishAckBlocksNewPublish(s_publishAckState);
    portEXIT_CRITICAL(&s_publishAckMux);
    return blocked;
}

void resetAckStateUnlessPending() {
    portENTER_CRITICAL(&s_publishAckMux);
    if (!mqttPublishAckIsReserved(s_publishAckState)) {
        s_publishAckState = MqttPublishAckState{};
    }
    portEXIT_CRITICAL(&s_publishAckMux);
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

void failPendingPublishAckIfPending(uint32_t clientGeneration) {
    portENTER_CRITICAL(&s_publishAckMux);
    const bool failed = mqttPublishAckFailIfPending(&s_publishAckState, clientGeneration);
    portEXIT_CRITICAL(&s_publishAckMux);
    if (failed) {
        s_ackTimerArmed.store(false, std::memory_order_release);
        completePublishAsync(PublishAsyncState::Fail);
    }
}

void applySuccessfulPublishSideEffects(int expected) {
    // Apply outside the ack spinlock: heart mux + SSE must not nest under it (RC-MQTT-03).
    if (!heartSentCounterApplyAfterSuccessfulPublish(expected)) {
        return;
    }
    audioRequest(AudioMsg::Kind::Tx);
    (void)displayRequest(DisplayMsg::Cmd::DrawHeart, DisplayRequestMode::Content, 0U);
}

} // namespace

void mqttHandlePublishedAck(int messageId, uint32_t clientGeneration) {
    int expected = 0;
    portENTER_CRITICAL(&s_publishAckMux);
    const bool confirmed = mqttPublishAckConfirm(&s_publishAckState, messageId, clientGeneration);
    if (confirmed) {
        expected = s_publishAckState.expectedCounter;
        completePublishAsync(PublishAsyncState::Ok);
    }
    portEXIT_CRITICAL(&s_publishAckMux);
    if (!confirmed) {
        return;
    }
    s_ackTimerArmed.store(false, std::memory_order_release);
    applySuccessfulPublishSideEffects(expected);
}

void mqttAbortPendingPublish(uint32_t clientGeneration) { failPendingPublishAckIfPending(clientGeneration); }

void mqttAbortPendingPublish() {
    uint32_t generation = 0;
    portENTER_CRITICAL(&s_publishAckMux);
    const bool ackPending = mqttPublishAckIsPending(s_publishAckState);
    if (ackPending) {
        generation = s_publishAckState.clientGeneration;
        portEXIT_CRITICAL(&s_publishAckMux);
        // failPendingPublishAck no-op if Confirm already set Acked — do not CAS Ok→Fail.
        failPendingPublishAck(generation);
        return;
    }
    // BUG-MQTT-10: Starting stays reserved so Attach can bind msg-id after publish returns.
    uint8_t expected = static_cast<uint8_t>(PublishAsyncState::Pending);
    (void)s_publishAsync.compare_exchange_strong(expected, static_cast<uint8_t>(PublishAsyncState::Fail),
                                                 std::memory_order_acq_rel);
    portEXIT_CRITICAL(&s_publishAckMux);
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
    failPendingPublishAckIfPending(generation);
}

/** Start QoS-1 publish; does not block the network task on PUBACK (STAB-04 / PERF-01). */
static MqttChayaPublishTry mqttPublishChayaLocked() {
    if (g_systemShutdownInProgress.load(std::memory_order_acquire)) {
        ESP_LOGW(TAG, "Publish skipped: shutdown in progress");
        return MqttChayaPublishTry::Fail;
    }
    if (s_mqttPublishBlocked.load(std::memory_order_acquire) ||
        s_mqttKillCoalesce.load(std::memory_order_acquire)) {
        ESP_LOGW(TAG, "Publish skipped: broker settings changing");
        return MqttChayaPublishTry::Fail;
    }
    if (!s_connected.load(std::memory_order_acquire)) {
        ESP_LOGW(TAG, "Publish skipped: not connected");
        return MqttChayaPublishTry::Retry;
    }
    if (publishAckBlocksNewPublish()) {
        ESP_LOGW(TAG, "Publish skipped: previous QoS 1 acknowledgement pending");
        return MqttChayaPublishTry::Retry;
    }
    char topicPub[sizeof(MqttConfig::topicPub)]{};
    mqttCfgTopicPubLockedCopy(topicPub, sizeof(topicPub));

    char buf[16];
    const int cur = heartSentCounter.load(std::memory_order_relaxed);
    if (cur >= INT_MAX) {
        ESP_LOGW(TAG, "Publish skipped: heartSentCounter at maximum");
        return MqttChayaPublishTry::Fail;
    }
    const int nextVal = heartSentCounterNextPure(cur);
    static_cast<void>(snprintf(buf, sizeof(buf), "%d", nextVal));

    if (!mqttClientLockTimed()) {
        ESP_LOGW(TAG, "Publish skipped: mqtt client mutex timeout");
        return MqttChayaPublishTry::Retry;
    }
    const esp_mqtt_client_handle_t cli = s_client.load(std::memory_order_acquire);
    const uint32_t clientGeneration = s_clientGeneration.load(std::memory_order_acquire);
    if (cli == nullptr) {
        mqttClientUnlock();
        ESP_LOGW(TAG, "Publish skipped: mqtt client null");
        return MqttChayaPublishTry::Retry;
    }

    bool reserved = false;
    portENTER_CRITICAL(&s_publishAckMux);
    const bool canReserve = mqttPublishAckBeginAllowed(
        mqttPublishAckCanBegin(s_publishAckState),
        s_publishAsync.load(std::memory_order_acquire) == static_cast<uint8_t>(PublishAsyncState::Pending));
    if (canReserve) {
        reserved = mqttPublishAckReserve(&s_publishAckState, clientGeneration, nextVal);
    }
    portEXIT_CRITICAL(&s_publishAckMux);
    if (!reserved) {
        mqttClientUnlock();
        ESP_LOGW(TAG, "Publish skipped: previous QoS 1 acknowledgement pending");
        return MqttChayaPublishTry::Fail;
    }
    mqttClientUnlock();

    const int pid = esp_mqtt_client_publish(cli, topicPub, buf, static_cast<int>(strlen(buf)), 1, 1);
    bool published = false;
    bool alreadyAcked = false;
    int expected = 0;
    if (pid >= 0) {
        portENTER_CRITICAL(&s_publishAckMux);
        const bool genOk = s_clientGeneration.load(std::memory_order_acquire) == clientGeneration;
        published = genOk && mqttPublishAckAttach(&s_publishAckState, pid, clientGeneration);
        if (published) {
            alreadyAcked = mqttPublishAckWasConfirmed(s_publishAckState, pid, clientGeneration);
            if (alreadyAcked) {
                expected = s_publishAckState.expectedCounter;
                completePublishAsync(PublishAsyncState::Ok);
            }
        }
        portEXIT_CRITICAL(&s_publishAckMux);
    } else {
        failPendingPublishAck(clientGeneration);
        ESP_LOGW(TAG, "Publish failed: esp_mqtt_client_publish returned %d", pid);
        return MqttChayaPublishTry::Fail;
    }
    if (!published) {
        failPendingPublishAck(clientGeneration);
        ESP_LOGW(TAG, "Publish skipped: could not begin PUBACK wait (msg_id=%d)", pid);
        return MqttChayaPublishTry::Fail;
    }
    if (alreadyAcked) {
        s_ackTimerArmed.store(false, std::memory_order_release);
        applySuccessfulPublishSideEffects(expected);
        ESP_LOGD(TAG, "Published chaya QoS 1 → %s payload=%s (msg_id=%d, late PUBACK)", topicPub, buf, pid);
        return MqttChayaPublishTry::Ok;
    }
    s_publishAckStartedMs.store(millis(), std::memory_order_release);
    s_ackTimerArmed.store(true, std::memory_order_release);
    ESP_LOGD(TAG, "Published chaya QoS 1 → %s payload=%s (msg_id=%d)", topicPub, buf, pid);
    return MqttChayaPublishTry::Ok;
}

MqttChayaPublishTry mqttPublishChayaAndApplySentCounters() {
    if (g_chayaPublishMutex == nullptr) {
        return MqttChayaPublishTry::Fail;
    }
    if (xSemaphoreTake(g_chayaPublishMutex, kChayaPublishLockTimeoutTicks) != pdTRUE) {
        ESP_LOGW(TAG, "Publish skipped: chaya mutex timeout");
        return MqttChayaPublishTry::Retry;
    }
    const MqttChayaPublishTry result = mqttPublishChayaLocked();
    xSemaphoreGive(g_chayaPublishMutex);
    return result;
}

MqttChayaPublishAsync mqttRequestChayaPublishAsync() {
    if (g_systemShutdownInProgress.load(std::memory_order_acquire)) {
        mqttAbortPendingPublish();
        return MqttChayaPublishAsync::Fail;
    }
    uint8_t expected = static_cast<uint8_t>(PublishAsyncState::Idle);
    if (s_publishAsync.compare_exchange_strong(expected, static_cast<uint8_t>(PublishAsyncState::Pending),
                                               std::memory_order_acq_rel)) {
        resetAckStateUnlessPending();
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
    if (publishAckBlocksNewPublish()) {
        return;
    }
    // Start only — PUBACK / timeout complete Ok/Fail asynchronously (STAB-04 / PERF-01).
    if (mqttChayaPublishTryIsFail(mqttPublishChayaAndApplySentCounters())) {
        completePublishAsync(PublishAsyncState::Fail);
    }
}

bool mqttChayaPublishAsyncIsPending() {
    return s_publishAsync.load(std::memory_order_acquire) == static_cast<uint8_t>(PublishAsyncState::Pending);
}

void mqttClearChayaPublishAsync() {
    s_publishAsync.store(static_cast<uint8_t>(PublishAsyncState::Idle), std::memory_order_release);
}

bool mqttPublishBlocked() {
    return s_mqttPublishBlocked.load(std::memory_order_acquire) ||
           s_mqttKillCoalesce.load(std::memory_order_acquire);
}

bool mqttKillClientPending() { return s_mqttKillCoalesce.load(std::memory_order_acquire); }

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
