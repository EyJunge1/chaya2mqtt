#include "mqtt_config.h"

#include "constants.h"
#include "nvs_utils.h"

#include "log_tag.h"

#include <Arduino.h>
#include <atomic>
#include <cstring>
#include <esp_log.h>
#include <freertos/portmacro.h>

DEFINE_LOG_TAG("MQTTCFG");

MqttConfig mqttCfg;

static MqttConfig     s_mqttPendingCfg{};
static portMUX_TYPE   s_mqttCfgMux      = portMUX_INITIALIZER_UNLOCKED;
static std::atomic<bool> s_mqttCfgDirty{true};

namespace {
constexpr const char kNvMqtt[] = "mqtt";
} // namespace

static void mqttCfgMarkDirty() {
    s_mqttCfgDirty.store(true, std::memory_order_release);
}

bool mqttCfgConsumeDirtySnapshotNeeded() {
    return s_mqttCfgDirty.exchange(false, std::memory_order_acq_rel);
}

void mqttCfgSnapshot(MqttConfig* out) {
    if (out == nullptr) {
        return;
    }
    portENTER_CRITICAL(&s_mqttCfgMux);
    *out = mqttCfg;
    portEXIT_CRITICAL(&s_mqttCfgMux);
}

void mqttCfgStorePending(const MqttConfig* pending) {
    if (pending == nullptr) {
        return;
    }
    portENTER_CRITICAL(&s_mqttCfgMux);
    s_mqttPendingCfg = *pending;
    portEXIT_CRITICAL(&s_mqttCfgMux);
}

void mqttCfgApplyPendingToActive() {
    portENTER_CRITICAL(&s_mqttCfgMux);
    mqttCfg = s_mqttPendingCfg;
    portEXIT_CRITICAL(&s_mqttCfgMux);
    mqttCfgMarkDirty();
}

void loadMQTTConfig() {
    if (!app_nvs::namespaceExists(kNvMqtt)) {
        ESP_LOGI(TAG, "NVS mqtt namespace not present, using MQTT defaults");
        strlcpy(mqttCfg.topicPub, kMqttDefaultTopicPub, sizeof(mqttCfg.topicPub));
        strlcpy(mqttCfg.topicSub, kMqttDefaultTopicSub, sizeof(mqttCfg.topicSub));
        mqttCfgMarkDirty();
        return;
    }
    if (!app_nvs::hasKey(kNvMqtt, "server")) {
        ESP_LOGI(TAG, "MQTT not configured yet in NVS, using defaults");
        strlcpy(mqttCfg.topicPub, kMqttDefaultTopicPub, sizeof(mqttCfg.topicPub));
        strlcpy(mqttCfg.topicSub, kMqttDefaultTopicSub, sizeof(mqttCfg.topicSub));
        mqttCfgMarkDirty();
        return;
    }
    static_cast<void>(
        app_nvs::readString(kNvMqtt, "server", mqttCfg.server, sizeof(mqttCfg.server)));
    const int portRaw = app_nvs::readInt(kNvMqtt, "port", static_cast<int>(kMqttDefaultTlsPort));
    mqttCfg.port = normalizeMqttPort(portRaw);
    static_cast<void>(
        app_nvs::readString(kNvMqtt, "user", mqttCfg.username, sizeof(mqttCfg.username)));
    static_cast<void>(
        app_nvs::readString(kNvMqtt, "pass", mqttCfg.password, sizeof(mqttCfg.password)));

    if (app_nvs::readString(kNvMqtt, "topic_pub", mqttCfg.topicPub, sizeof(mqttCfg.topicPub))
            == 0U
        || mqttCfg.topicPub[0] == '\0') {
        strlcpy(mqttCfg.topicPub, kMqttDefaultTopicPub, sizeof(mqttCfg.topicPub));
    }
    if (app_nvs::readString(kNvMqtt, "topic_sub", mqttCfg.topicSub, sizeof(mqttCfg.topicSub))
            == 0U
        || mqttCfg.topicSub[0] == '\0') {
        strlcpy(mqttCfg.topicSub, kMqttDefaultTopicSub, sizeof(mqttCfg.topicSub));
    }
    mqttCfgMarkDirty();
}

void saveMQTTConfig() {
    MqttConfig snap{};
    mqttCfgSnapshot(&snap);

    bool ok = true;
    ok = ok && app_nvs::writeString(kNvMqtt, "server", snap.server);
    ok = ok && app_nvs::writeInt(kNvMqtt, "port", static_cast<int>(snap.port));
    ok = ok && app_nvs::writeString(kNvMqtt, "user", snap.username);
    ok = ok && app_nvs::writeString(kNvMqtt, "pass", snap.password);
    ok = ok && app_nvs::writeString(kNvMqtt, "topic_pub", snap.topicPub);
    ok = ok && app_nvs::writeString(kNvMqtt, "topic_sub", snap.topicSub);
    if (!ok) {
        ESP_LOGE(TAG, "NVS mqtt: persist failed");
    }
}
