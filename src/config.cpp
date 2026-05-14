#include "config.h"

#include <Arduino.h>
#include <Preferences.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <esp_log.h>

#if defined(CORE_DEBUG_LEVEL) && CORE_DEBUG_LEVEL > 0
static const char* TAG = "CFG";
#else
static constexpr const char* TAG __attribute__((unused)) = "";
#endif

MqttConfig mqttCfg;

void loadMQTTConfig() {
    Preferences preferences;
    if (!preferences.begin("mqtt", true)) {
        ESP_LOGW(TAG, "NVS mqtt: lesen fehlgeschlagen, nutze Defaults");
        strlcpy(mqttCfg.topicPub, "chaya/to_b", sizeof(mqttCfg.topicPub));
        strlcpy(mqttCfg.topicSub, "chaya/to_a", sizeof(mqttCfg.topicSub));
        return;
    }
    if (!preferences.isKey("server")) {
        preferences.end();
        ESP_LOGI(TAG, "MQTT noch nicht konfiguriert, nutze Defaults");
        strlcpy(mqttCfg.topicPub, "chaya/to_b", sizeof(mqttCfg.topicPub));
        strlcpy(mqttCfg.topicSub, "chaya/to_a", sizeof(mqttCfg.topicSub));
        return;
    }
    preferences.getString("server", mqttCfg.server, sizeof(mqttCfg.server));
    const int p = preferences.getInt("port", 8883);
    mqttCfg.port = (p > 0 && p <= 65535) ? static_cast<uint16_t>(p) : 8883;
    preferences.getString("user", mqttCfg.username, sizeof(mqttCfg.username));
    preferences.getString("pass", mqttCfg.password, sizeof(mqttCfg.password));
    if (preferences.getString("topic_pub", mqttCfg.topicPub, sizeof(mqttCfg.topicPub)) == 0
        || mqttCfg.topicPub[0] == '\0') {
        strlcpy(mqttCfg.topicPub, "chaya/to_b", sizeof(mqttCfg.topicPub));
    }
    if (preferences.getString("topic_sub", mqttCfg.topicSub, sizeof(mqttCfg.topicSub)) == 0
        || mqttCfg.topicSub[0] == '\0') {
        strlcpy(mqttCfg.topicSub, "chaya/to_a", sizeof(mqttCfg.topicSub));
    }
    preferences.end();
}

void saveMQTTConfig() {
    Preferences preferences;
    if (!preferences.begin("mqtt", false)) {
        ESP_LOGE(TAG, "NVS mqtt: schreiben fehlgeschlagen");
        return;
    }
    preferences.putString("server", mqttCfg.server);
    preferences.putInt("port", mqttCfg.port);
    preferences.putString("user", mqttCfg.username);
    preferences.putString("pass", mqttCfg.password);
    preferences.putString("topic_pub", mqttCfg.topicPub);
    preferences.putString("topic_sub", mqttCfg.topicSub);
    preferences.end();
}
