#include "config.h"

#include "constants.h"
#include "config/nvs_keys.h"
#include "config/nvs_utils.h"

#include "log_tag.h"

#include <Arduino.h>
#include <Preferences.h>
#include <atomic>
#include <cstring>

#include <esp_mac.h>
#include <esp_log.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

DEFINE_LOG_TAG("MQTTCFG");

void buildDeviceId(char* out, size_t outLen);

// mqttCfg + optional web pending form; s_mqttCfgDirty forces mqttLoop snapshot refresh.

static MqttConfig          mqttCfg{};
static MqttConfig          s_mqttPendingCfg{};
static SemaphoreHandle_t   s_mqttCfgMutex       = nullptr;
static std::atomic<bool> s_mqttCfgDirty{true};

namespace {
inline void mqttCfgMutexEnsureCreated() {
    if (s_mqttCfgMutex != nullptr) {
        return;
    }
    s_mqttCfgMutex = xSemaphoreCreateMutex();
    if (s_mqttCfgMutex == nullptr) {
        ESP_LOGE(TAG, "mqtt cfg mutex alloc failed");
        abort();
    }
}

inline void mqttCfgLock() {
    mqttCfgMutexEnsureCreated();
    static_cast<void>(xSemaphoreTake(s_mqttCfgMutex, portMAX_DELAY));
}

inline void mqttCfgUnlock() {
    if (s_mqttCfgMutex != nullptr) {
        xSemaphoreGive(s_mqttCfgMutex);
    }
}

void mqttCfgSanitizePartnerId(MqttConfig& cfg) {
    if (cfg.partnerDeviceId[0] == '\0') {
        return;
    }
    for (char* p = cfg.partnerDeviceId; *p != '\0'; ++p) {
        if (*p >= 'A' && *p <= 'F') {
            *p = static_cast<char>(*p - 'A' + 'a');
        }
    }
    if (!deviceIdSyntaxOk(cfg.partnerDeviceId)) {
        ESP_LOGW(TAG, "Invalid partner device ID in NVS — cleared");
        cfg.partnerDeviceId[0] = '\0';
        return;
    }
    char ownId[kDeviceIdBufLen];
    buildDeviceId(ownId, sizeof(ownId));
    if (strcmp(cfg.partnerDeviceId, ownId) == 0) {
        ESP_LOGW(TAG, "Partner device ID equals own ID — cleared");
        cfg.partnerDeviceId[0] = '\0';
    }
}

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
    mqttCfgSanitizePartnerId(cfg);
    mqttCfgApplyPairingTopics(&cfg);
    cfg.port = normalizeMqttPort(static_cast<int>(cfg.port));
}

} // namespace

void buildDeviceId(char* out, size_t outLen) {
    if (out == nullptr || outLen < kDeviceIdBufLen) {
        if (out != nullptr && outLen > 0U) {
            out[0] = '\0';
        }
        return;
    }
    uint8_t mac[6] = {};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
        out[0] = '\0';
        return;
    }
    static_cast<void>(snprintf(out, outLen, "%02x%02x%02x", mac[3], mac[4], mac[5]));
}

void mqttCfgApplyPairingTopics(MqttConfig* cfg) {
    if (cfg == nullptr || cfg->partnerDeviceId[0] == '\0') {
        return;
    }
    if (!deviceIdSyntaxOk(cfg->partnerDeviceId)) {
        return;
    }
    char ownId[kDeviceIdBufLen];
    buildDeviceId(ownId, sizeof(ownId));
    if (!deviceIdSyntaxOk(ownId)) {
        return;
    }
    static_cast<void>(snprintf(cfg->topicPub, sizeof(cfg->topicPub), "%s%s", kMqttPairTopicPrefix, ownId));
    static_cast<void>(snprintf(cfg->topicSub, sizeof(cfg->topicSub), "%s%s", kMqttPairTopicPrefix,
                               cfg->partnerDeviceId));
}

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
    mqttCfgLock();
    *out = mqttCfg;
    mqttCfgUnlock();
}

bool mqttCfgSnapshotTimed(MqttConfig* out, uint32_t timeoutMs) {
    if (out == nullptr) {
        return false;
    }
    mqttCfgMutexEnsureCreated();
    if (xSemaphoreTake(s_mqttCfgMutex, pdMS_TO_TICKS(timeoutMs)) != pdTRUE) {
        return false;
    }
    *out = mqttCfg;
    xSemaphoreGive(s_mqttCfgMutex);
    return true;
}

bool mqttCfgEquals(const MqttConfig* a, const MqttConfig* b) {
    if (a == nullptr || b == nullptr) {
        return false;
    }
    return memcmp(a, b, sizeof(MqttConfig)) == 0;
}

bool mqttCfgIsBrokerConfigured() {
    mqttCfgLock();
    const bool ok = mqttCfg.server[0] != '\0';
    mqttCfgUnlock();
    return ok;
}

void mqttCfgTopicPubLockedCopy(char* out, size_t outLen) {
    if (out == nullptr || outLen == 0U) {
        return;
    }
    mqttCfgLock();
    strlcpy(out, mqttCfg.topicPub, outLen);
    mqttCfgUnlock();
}

void mqttCfgStorePending(const MqttConfig* pending) {
    if (pending == nullptr) {
        return;
    }
    mqttCfgLock();
    s_mqttPendingCfg = *pending;
    mqttCfgUnlock();
}

void mqttCfgApplyPendingToActive() {
    mqttCfgLock();
    mqttCfg = s_mqttPendingCfg;
    mqttCfgUnlock();
    mqttCfgMarkDirty();
}

void mqttCfgPendingSnapshot(MqttConfig* out) {
    if (out == nullptr) {
        return;
    }
    mqttCfgLock();
    *out = s_mqttPendingCfg;
    mqttCfgUnlock();
}

bool mqttCfgHasUnappliedPending() {
    mqttCfgLock();
    const bool differs = memcmp(&mqttCfg, &s_mqttPendingCfg, sizeof(MqttConfig)) != 0;
    mqttCfgUnlock();
    return differs;
}

bool mqttCfgMatchesNvs() {
    MqttConfig active{};
    mqttCfgSnapshot(&active);

    app_nvs::ScopedNvsLock lock;
    Preferences prefs;
    if (!prefs.begin(kNvsNsMqtt, true)) {
        return active.server[0] == '\0';
    }

    MqttConfig stored{};
    prefs.getString(kNvsKeyMqttServer, stored.server, sizeof(stored.server));
    stored.port =
        static_cast<uint16_t>(prefs.getInt(kNvsKeyMqttPort, static_cast<int>(kMqttDefaultTlsPort)));
    prefs.getString(kNvsKeyMqttUser, stored.username, sizeof(stored.username));
    prefs.getString(kNvsKeyMqttPass, stored.password, sizeof(stored.password));
    prefs.getString(kNvsKeyMqttTopicPub, stored.topicPub, sizeof(stored.topicPub));
    prefs.getString(kNvsKeyMqttTopicSub, stored.topicSub, sizeof(stored.topicSub));
    prefs.getString(kNvsKeyMqttPartnerId, stored.partnerDeviceId, sizeof(stored.partnerDeviceId));
    prefs.end();

    mqttCfgSanitizeAfterNvsLoad(stored);
    return memcmp(&active, &stored, sizeof(MqttConfig)) == 0;
}

namespace {

bool mqttCfgPutStringOrEmpty(Preferences& prefs, const char* key, const char* value) {
    const char* v = (value != nullptr) ? value : "";
    const size_t w = prefs.putString(key, v);
    return w > 0U || v[0] == '\0';
}

} // namespace

void loadMQTTConfig() {
    MqttConfig loaded{};

    {
        app_nvs::ScopedNvsLock lock;
        Preferences prefs;
        if (!prefs.begin(kNvsNsMqtt, true)) {
            ESP_LOGI(TAG, "NVS mqtt namespace not present, using MQTT defaults");
            strlcpy(loaded.topicPub, kMqttDefaultTopicPub, sizeof(loaded.topicPub));
            strlcpy(loaded.topicSub, kMqttDefaultTopicSub, sizeof(loaded.topicSub));
            loaded.server[0]   = '\0';
            loaded.username[0] = '\0';
            loaded.password[0] = '\0';
            loaded.port        = kMqttDefaultTlsPort;
            mqttCfgSanitizeAfterNvsLoad(loaded);
            mqttCfgLock();
            mqttCfg = loaded;
            s_mqttPendingCfg = loaded;
            mqttCfgUnlock();
            mqttCfgMarkDirty();
            return;
        }
        if (!prefs.isKey(kNvsKeyMqttServer)) {
            ESP_LOGI(TAG, "MQTT not configured yet in NVS, using defaults");
            strlcpy(loaded.topicPub, kMqttDefaultTopicPub, sizeof(loaded.topicPub));
            strlcpy(loaded.topicSub, kMqttDefaultTopicSub, sizeof(loaded.topicSub));
            loaded.server[0] = '\0';
            prefs.end();
            mqttCfgSanitizeAfterNvsLoad(loaded);
            mqttCfgLock();
            mqttCfg = loaded;
            s_mqttPendingCfg = loaded;
            mqttCfgUnlock();
            mqttCfgMarkDirty();
            return;
        }

        prefs.getString(kNvsKeyMqttServer, loaded.server, sizeof(loaded.server));
        loaded.port =
            normalizeMqttPort(prefs.getInt(kNvsKeyMqttPort, static_cast<int>(kMqttDefaultTlsPort)));
        prefs.getString(kNvsKeyMqttUser, loaded.username, sizeof(loaded.username));
        prefs.getString(kNvsKeyMqttPass, loaded.password, sizeof(loaded.password));
        const size_t tpLen =
            prefs.getString(kNvsKeyMqttTopicPub, loaded.topicPub, sizeof(loaded.topicPub));
        if (tpLen == 0U || loaded.topicPub[0] == '\0') {
            strlcpy(loaded.topicPub, kMqttDefaultTopicPub, sizeof(loaded.topicPub));
        }
        const size_t tsLen =
            prefs.getString(kNvsKeyMqttTopicSub, loaded.topicSub, sizeof(loaded.topicSub));
        if (tsLen == 0U || loaded.topicSub[0] == '\0') {
            strlcpy(loaded.topicSub, kMqttDefaultTopicSub, sizeof(loaded.topicSub));
        }
        prefs.getString(kNvsKeyMqttPartnerId, loaded.partnerDeviceId, sizeof(loaded.partnerDeviceId));
        prefs.end();
    }

    mqttCfgSanitizeAfterNvsLoad(loaded);
    mqttCfgLock();
    mqttCfg = loaded;
    s_mqttPendingCfg = loaded;
    mqttCfgUnlock();
    mqttCfgMarkDirty();
}

// User/password are stored as plain Preferences strings; see docs/SECURITY.md (NVS / flash access).
bool saveMQTTConfig() {
    MqttConfig snap{};
    mqttCfgSnapshot(&snap);

    app_nvs::ScopedNvsLock lock;
    Preferences prefs;
    if (!prefs.begin(kNvsNsMqtt, false)) {
        ESP_LOGE(TAG, "NVS mqtt: begin failed");
        return false;
    }
    bool ok = mqttCfgPutStringOrEmpty(prefs, kNvsKeyMqttServer, snap.server);
    if (!ok) {
        prefs.end();
        return false;
    }
    ok = (prefs.putInt(kNvsKeyMqttPort, static_cast<int>(snap.port)) > 0);
    if (!ok) {
        prefs.end();
        return false;
    }
    ok = mqttCfgPutStringOrEmpty(prefs, kNvsKeyMqttUser, snap.username);
    if (!ok) {
        prefs.end();
        return false;
    }
    ok = mqttCfgPutStringOrEmpty(prefs, kNvsKeyMqttPass, snap.password);
    if (!ok) {
        prefs.end();
        return false;
    }
    ok = mqttCfgPutStringOrEmpty(prefs, kNvsKeyMqttTopicPub, snap.topicPub);
    if (!ok) {
        prefs.end();
        return false;
    }
    ok = mqttCfgPutStringOrEmpty(prefs, kNvsKeyMqttTopicSub, snap.topicSub);
    if (!ok) {
        prefs.end();
        return false;
    }
    ok = mqttCfgPutStringOrEmpty(prefs, kNvsKeyMqttPartnerId, snap.partnerDeviceId);
    prefs.end();
    if (!ok) {
        ESP_LOGE(TAG, "NVS mqtt: persist failed");
        return false;
    }
    return true;
}
