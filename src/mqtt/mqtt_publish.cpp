#include "mqtt.h"

#include "mqtt_internal.h"
#include "mqtt_publish_ack.h"

#include "async/task_handles.h"
#include "config.h"
#include "audio/audio.h"
#include "diag/task_watchdog.h"
#include "display/display.h"
#include "heart/counter.h"

#include <climits>
#include <cstdio>
#include <cstring>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <mqtt_client.h>

#include "util/log_tag.h"

DEFINE_LOG_TAG("MQTT");

static std::atomic<bool> s_mqttPublishBlocked{false};
static portMUX_TYPE       s_publishAckMux = portMUX_INITIALIZER_UNLOCKED;
static MqttPublishAckState s_publishAckState{};

static bool publishAckPending() {
    portENTER_CRITICAL(&s_publishAckMux);
    const bool pending = mqttPublishAckIsPending(s_publishAckState);
    portEXIT_CRITICAL(&s_publishAckMux);
    return pending;
}

static bool publishAckConfirmed(int messageId, uint32_t clientGeneration) {
    portENTER_CRITICAL(&s_publishAckMux);
    const bool confirmed =
        mqttPublishAckWasConfirmed(s_publishAckState, messageId, clientGeneration);
    portEXIT_CRITICAL(&s_publishAckMux);
    return confirmed;
}

void mqttHandlePublishedAck(int messageId, uint32_t clientGeneration) {
    portENTER_CRITICAL(&s_publishAckMux);
    const bool confirmed =
        mqttPublishAckConfirm(&s_publishAckState, messageId, clientGeneration);
    portEXIT_CRITICAL(&s_publishAckMux);
    if (!confirmed) {
        return;
    }

    heartSentCounterApplyAfterSuccessfulPublish();
    maybeSaveHeartSentCounter();
    audioRequest(AudioMsg::Kind::Tx);
    requestHeartRedraw();
    if (g_chayaPubAckSemaphore != nullptr) {
        xSemaphoreGive(g_chayaPubAckSemaphore);
    }
}

void mqttAbortPendingPublish(uint32_t clientGeneration) {
    portENTER_CRITICAL(&s_publishAckMux);
    const bool failed = mqttPublishAckFail(&s_publishAckState, clientGeneration);
    portEXIT_CRITICAL(&s_publishAckMux);
    if (failed && g_chayaPubAckSemaphore != nullptr) {
        xSemaphoreGive(g_chayaPubAckSemaphore);
    }
}

static bool mqttPublishChayaLocked() {
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
    if (g_chayaPubAckSemaphore == nullptr) {
        return false;
    }
    while (xSemaphoreTake(g_chayaPubAckSemaphore, 0) == pdTRUE) {
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

    if (!mqttClientLockTimed()) {
        ESP_LOGW(TAG, "Publish skipped: mqtt client mutex timeout");
        return false;
    }
    esp_mqtt_client_handle_t cli = s_client;
    if (cli == nullptr) {
        mqttClientUnlock();
        return false;
    }

    const uint32_t clientGeneration = s_clientGeneration.load(std::memory_order_acquire);
    const int pid =
        esp_mqtt_client_publish(cli, topicPub, buf, static_cast<int>(strlen(buf)), 1, 1);
    bool published = false;
    if (pid >= 0) {
        portENTER_CRITICAL(&s_publishAckMux);
        published = mqttPublishAckBegin(&s_publishAckState, pid, clientGeneration,
                                        static_cast<int>(nextVal));
        portEXIT_CRITICAL(&s_publishAckMux);
    }
    mqttClientUnlock();
    if (!published) {
        return false;
    }
    ESP_LOGD(TAG, "Published chaya QoS 1 → %s payload=%s (msg_id=%d)", topicPub, buf, pid);

    const TickType_t started = xTaskGetTickCount();
    const TickType_t timeout = pdMS_TO_TICKS(kMqttPublishAckWaitMs);
    constexpr TickType_t kWaitSlice = pdMS_TO_TICKS(250);
    while ((xTaskGetTickCount() - started) < timeout) {
        const TickType_t elapsed   = xTaskGetTickCount() - started;
        const TickType_t remaining = timeout - elapsed;
        const TickType_t waitTicks = remaining < kWaitSlice ? remaining : kWaitSlice;
        if (xSemaphoreTake(g_chayaPubAckSemaphore, waitTicks) == pdTRUE) {
            break;
        }
        chayaTaskWatchdogReset();
    }
    chayaTaskWatchdogReset();
    const bool confirmed = publishAckConfirmed(pid, clientGeneration);
    if (!confirmed) {
        ESP_LOGW(TAG, "QoS 1 PUBACK timeout/failure (msg_id=%d)", pid);
    }
    return confirmed;
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

bool mqttPublishBlocked() {
    return s_mqttPublishBlocked.load(std::memory_order_acquire);
}

void mqttBeginSettingsApply() {
    s_mqttPublishBlocked.store(true, std::memory_order_release);
}

void mqttEndSettingsApply() {
    s_mqttPublishBlocked.store(false, std::memory_order_release);
}
