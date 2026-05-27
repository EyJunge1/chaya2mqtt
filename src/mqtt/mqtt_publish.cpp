#include "mqtt.h"

#include "mqtt_internal.h"

#include "async/task_handles.h"
#include "config.h"
#include "display/display.h"
#include "heart/counter.h"

#include <algorithm>
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

static bool mqttPublishChayaLocked() {
    if (s_mqttPublishBlocked.load(std::memory_order_acquire)) {
        ESP_LOGW(TAG, "Publish skipped: broker settings changing");
        return false;
    }
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

    if (!mqttClientLockTimed()) {
        ESP_LOGW(TAG, "Publish skipped: mqtt client mutex timeout");
        return false;
    }
    esp_mqtt_client_handle_t cli = s_client;
    if (cli == nullptr) {
        mqttClientUnlock();
        return false;
    }

    const int pid = esp_mqtt_client_publish(cli, topicPub, buf, static_cast<int>(strlen(buf)), 0, 1);
    const bool published = pid >= 0;
    mqttClientUnlock();
    if (published) {
        ESP_LOGD(TAG, "Published chaya → %s payload=%s (msg_id=%d)", topicPub, buf, pid);
    }
    return published && s_connected.load(std::memory_order_acquire);
}

bool mqttPublishChaya() {
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

bool mqttPublishChayaAndApplySentCounters() {
    if (g_chayaPublishMutex == nullptr) {
        return false;
    }
    if (xSemaphoreTake(g_chayaPublishMutex, kChayaPublishLockTimeoutTicks) != pdTRUE) {
        ESP_LOGW(TAG, "Publish skipped: chaya mutex timeout");
        return false;
    }
    const bool ok = mqttPublishChayaLocked();
    if (!ok) {
        xSemaphoreGive(g_chayaPublishMutex);
        return false;
    }
    heartSentCounterApplyAfterSuccessfulPublish();
    maybeSaveHeartSentCounter();
    requestHeartRedraw();
    xSemaphoreGive(g_chayaPublishMutex);
    return true;
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
