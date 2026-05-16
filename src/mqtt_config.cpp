#include "mqtt_config.h"

#include <Arduino.h>
#include <Preferences.h>
#include <cstring>
#include <esp_log.h>
#include <freertos/portmacro.h>

#if defined(CORE_DEBUG_LEVEL) && CORE_DEBUG_LEVEL > 0
static const char* TAG = "CFG";
#else
static constexpr const char* TAG __attribute__((unused)) = "";
#endif

MqttConfig mqttCfg;

static MqttConfig           s_mqttPendingCfg{};
static portMUX_TYPE s_mqttCfgMux = portMUX_INITIALIZER_UNLOCKED;

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
}

void loadMQTTConfig() {
    Preferences preferences;
    if (!preferences.begin("mqtt", true)) {
        ESP_LOGI(TAG, "NVS mqtt namespace not present yet, using defaults");
        strlcpy(mqttCfg.topicPub, kMqttDefaultTopicPub, sizeof(mqttCfg.topicPub));
        strlcpy(mqttCfg.topicSub, kMqttDefaultTopicSub, sizeof(mqttCfg.topicSub));
        return;
    }
    if (!preferences.isKey("server")) {
        preferences.end();
        ESP_LOGI(TAG, "MQTT noch nicht konfiguriert, nutze Defaults");
        strlcpy(mqttCfg.topicPub, kMqttDefaultTopicPub, sizeof(mqttCfg.topicPub));
        strlcpy(mqttCfg.topicSub, kMqttDefaultTopicSub, sizeof(mqttCfg.topicSub));
        return;
    }
    preferences.getString("server", mqttCfg.server, sizeof(mqttCfg.server));
    const int p = preferences.getInt("port", static_cast<int>(kMqttDefaultTlsPort));
    mqttCfg.port = normalizeMqttPort(p);
    preferences.getString("user", mqttCfg.username, sizeof(mqttCfg.username));
    preferences.getString("pass", mqttCfg.password, sizeof(mqttCfg.password));
    if (preferences.getString("topic_pub", mqttCfg.topicPub, sizeof(mqttCfg.topicPub)) == 0
        || mqttCfg.topicPub[0] == '\0') {
        strlcpy(mqttCfg.topicPub, kMqttDefaultTopicPub, sizeof(mqttCfg.topicPub));
    }
    if (preferences.getString("topic_sub", mqttCfg.topicSub, sizeof(mqttCfg.topicSub)) == 0
        || mqttCfg.topicSub[0] == '\0') {
        strlcpy(mqttCfg.topicSub, kMqttDefaultTopicSub, sizeof(mqttCfg.topicSub));
    }
    preferences.end();
}

void saveMQTTConfig() {
    MqttConfig snap{};
    mqttCfgSnapshot(&snap);
    Preferences preferences;
    if (!preferences.begin("mqtt", false)) {
        ESP_LOGE(TAG, "NVS mqtt: schreiben fehlgeschlagen");
        return;
    }
    preferences.putString("server", snap.server);
    preferences.putInt("port", snap.port);
    preferences.putString("user", snap.username);
    preferences.putString("pass", snap.password);
    preferences.putString("topic_pub", snap.topicPub);
    preferences.putString("topic_sub", snap.topicSub);
    preferences.end();
}
