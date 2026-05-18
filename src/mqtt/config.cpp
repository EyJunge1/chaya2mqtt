#include "config.h"

#include "constants.h"
#include "config/nvs_utils.h"

#include "log_tag.h"

#include <Arduino.h>
#include <Preferences.h>
#include <atomic>
#include <cstring>
#include <esp_log.h>
#include <freertos/portmacro.h>

DEFINE_LOG_TAG("MQTTCFG");

// mqttCfg + optional web pending form; s_mqttCfgDirty forces mqttLoop snapshot refresh.

MqttConfig mqttCfg;

static MqttConfig     s_mqttPendingCfg{};
static portMUX_TYPE   s_mqttCfgMux      = portMUX_INITIALIZER_UNLOCKED;
static std::atomic<bool> s_mqttCfgDirty{true};

namespace {
constexpr const char kNvMqtt[] = "mqtt";

void mqttCfgSanitizeAfterNvsLoad(MqttConfig& cfg) {
    if (cfg.server[0] != '\0' && !mqttServerSyntaxOk(cfg.server, sizeof(cfg.server))) {
        ESP_LOGW(TAG, "Invalid MQTT server in NVS — cleared");
        cfg.server[0] = '\0';
    }
    if (!mqttTopicSyntaxOk(cfg.topicPub, sizeof(cfg.topicPub))) {
        ESP_LOGW(TAG, "Invalid MQTT pub topic in NVS — using default");
        strlcpy(cfg.topicPub, kMqttDefaultTopicPub, sizeof(cfg.topicPub));
    }
    if (!mqttTopicSyntaxOk(cfg.topicSub, sizeof(cfg.topicSub))) {
        ESP_LOGW(TAG, "Invalid MQTT sub topic in NVS — using default");
        strlcpy(cfg.topicSub, kMqttDefaultTopicSub, sizeof(cfg.topicSub));
    }
    if (strcmp(cfg.topicPub, cfg.topicSub) == 0) {
        ESP_LOGW(TAG, "MQTT pub/sub topics equal in NVS — resetting to defaults");
        strlcpy(cfg.topicPub, kMqttDefaultTopicPub, sizeof(cfg.topicPub));
        strlcpy(cfg.topicSub, kMqttDefaultTopicSub, sizeof(cfg.topicSub));
    }
    cfg.port = normalizeMqttPort(static_cast<int>(cfg.port));
}
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

bool mqttCfgIsBrokerConfigured() {
    portENTER_CRITICAL(&s_mqttCfgMux);
    const bool ok = mqttCfg.server[0] != '\0';
    portEXIT_CRITICAL(&s_mqttCfgMux);
    return ok;
}

void mqttCfgTopicPubLockedCopy(char* out, size_t outLen) {
    if (out == nullptr || outLen == 0U) {
        return;
    }
    portENTER_CRITICAL(&s_mqttCfgMux);
    strlcpy(out, mqttCfg.topicPub, outLen);
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
    MqttConfig loaded{};

    {
        app_nvs::ScopedNvsLock lock;
        Preferences prefs;
        if (!prefs.begin(kNvMqtt, true)) {
            ESP_LOGI(TAG, "NVS mqtt namespace not present, using MQTT defaults");
            strlcpy(loaded.topicPub, kMqttDefaultTopicPub, sizeof(loaded.topicPub));
            strlcpy(loaded.topicSub, kMqttDefaultTopicSub, sizeof(loaded.topicSub));
            loaded.server[0]   = '\0';
            loaded.username[0] = '\0';
            loaded.password[0] = '\0';
            loaded.port        = kMqttDefaultTlsPort;
            mqttCfgSanitizeAfterNvsLoad(loaded);
            portENTER_CRITICAL(&s_mqttCfgMux);
            mqttCfg = loaded;
            portEXIT_CRITICAL(&s_mqttCfgMux);
            mqttCfgMarkDirty();
            return;
        }
        if (!prefs.isKey("server")) {
            ESP_LOGI(TAG, "MQTT not configured yet in NVS, using defaults");
            strlcpy(loaded.topicPub, kMqttDefaultTopicPub, sizeof(loaded.topicPub));
            strlcpy(loaded.topicSub, kMqttDefaultTopicSub, sizeof(loaded.topicSub));
            loaded.server[0] = '\0';
            prefs.end();
            mqttCfgSanitizeAfterNvsLoad(loaded);
            portENTER_CRITICAL(&s_mqttCfgMux);
            mqttCfg = loaded;
            portEXIT_CRITICAL(&s_mqttCfgMux);
            mqttCfgMarkDirty();
            return;
        }

        prefs.getString("server", loaded.server, sizeof(loaded.server));
        loaded.port = normalizeMqttPort(prefs.getInt("port", static_cast<int>(kMqttDefaultTlsPort)));
        prefs.getString("user", loaded.username, sizeof(loaded.username));
        prefs.getString("pass", loaded.password, sizeof(loaded.password));
        const size_t tpLen = prefs.getString("topic_pub", loaded.topicPub, sizeof(loaded.topicPub));
        if (tpLen == 0U || loaded.topicPub[0] == '\0') {
            strlcpy(loaded.topicPub, kMqttDefaultTopicPub, sizeof(loaded.topicPub));
        }
        const size_t tsLen = prefs.getString("topic_sub", loaded.topicSub, sizeof(loaded.topicSub));
        if (tsLen == 0U || loaded.topicSub[0] == '\0') {
            strlcpy(loaded.topicSub, kMqttDefaultTopicSub, sizeof(loaded.topicSub));
        }
        prefs.end();
    }

    mqttCfgSanitizeAfterNvsLoad(loaded);
    portENTER_CRITICAL(&s_mqttCfgMux);
    mqttCfg = loaded;
    portEXIT_CRITICAL(&s_mqttCfgMux);
    mqttCfgMarkDirty();
}

bool saveMQTTConfig() {
    MqttConfig snap{};
    mqttCfgSnapshot(&snap);

    app_nvs::ScopedNvsLock lock;
    Preferences prefs;
    if (!prefs.begin(kNvMqtt, false)) {
        ESP_LOGE(TAG, "NVS mqtt: begin failed");
        return false;
    }
    bool ok = prefs.putString("server", snap.server) > 0U;
    if (!ok) {
        prefs.end();
        return false;
    }
    ok = (prefs.putInt("port", static_cast<int>(snap.port)) > 0);
    if (!ok) {
        prefs.end();
        return false;
    }
    ok = prefs.putString("user", snap.username) > 0U;
    if (!ok) {
        prefs.end();
        return false;
    }
    ok = prefs.putString("pass", snap.password) > 0U;
    if (!ok) {
        prefs.end();
        return false;
    }
    ok = prefs.putString("topic_pub", snap.topicPub) > 0U;
    if (!ok) {
        prefs.end();
        return false;
    }
    ok = prefs.putString("topic_sub", snap.topicSub) > 0U;
    prefs.end();
    if (!ok) {
        ESP_LOGE(TAG, "NVS mqtt: persist failed");
        return false;
    }
    return true;
}
