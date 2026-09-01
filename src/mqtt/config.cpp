#include "config.h"

#include "pairing.h"

#include "async/sse_dirty.h"
#include "config/nvs_keys.h"
#include "config/nvs_utils.h"
#include "identity/device_identity.h"

#include "util/log_tag.h"

#include <Arduino.h>
#include <Preferences.h>
#include <atomic>
#include <cstring>

#include <esp_log.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

DEFINE_LOG_TAG("MQTTCFG");

// mqttCfg + optional web pending form; s_mqttCfgDirty forces mqttLoop snapshot refresh.

static MqttConfig mqttCfg{};
static MqttConfig s_mqttPendingCfg{};
static SemaphoreHandle_t s_mqttCfgMutex = nullptr;
static std::atomic<bool> s_mqttCfgDirty{true};
static std::atomic<bool> s_mqttNvsWriteFailed{false};
static std::atomic<bool> s_mqttApplyPending{false};
static std::atomic<bool> s_brokerConfigured{false};
static std::atomic<bool> s_paired{false};

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

void mqttCfgSanitizeAfterNvsLoad(MqttConfig &cfg) {
    char ownId[kDeviceIdBufLen];
    buildDeviceId(ownId, sizeof(ownId));
    const bool hadServer = cfg.server[0] != '\0';
    const bool hadPartner = cfg.partnerDeviceId[0] != '\0';
    mqttSanitizeConfigAfterLoad(cfg, ownId);
    if (hadServer && cfg.server[0] == '\0') {
        ESP_LOGW(TAG, "Invalid MQTT server in NVS — cleared");
    }
    if (hadPartner && cfg.partnerDeviceId[0] == '\0') {
        ESP_LOGW(TAG, "Invalid or self partner device ID in NVS — cleared");
    }
}

bool mqttCfgPutStringOrEmpty(Preferences &prefs, const char *key, const char *value) {
    return app_nvs::putStringOk(prefs, key, value);
}

void mqttCfgRefreshFlagsLocked() {
    s_brokerConfigured.store(mqttCfg.server[0] != '\0', std::memory_order_release);
    s_paired.store(mqttCfg.partnerDeviceId[0] != '\0', std::memory_order_release);
}

} // namespace

void mqttCfgApplyPairingTopics(MqttConfig *cfg) {
    char ownId[kDeviceIdBufLen];
    buildDeviceId(ownId, sizeof(ownId));
    mqttApplyPairingTopicsWithIds(cfg, ownId);
}

static void mqttCfgMarkDirty() {
    s_mqttCfgDirty.store(true, std::memory_order_release);
    sseMarkDirty(kSseChaya | kSseMqtt);
}

bool mqttCfgConsumeDirtySnapshotNeeded() { return s_mqttCfgDirty.exchange(false, std::memory_order_acq_rel); }

void mqttCfgSetNvsWriteFailed(bool failed) { s_mqttNvsWriteFailed.store(failed, std::memory_order_release); }

bool mqttCfgNvsWriteFailed() { return s_mqttNvsWriteFailed.load(std::memory_order_acquire); }

void mqttCfgSetApplyPending(bool pending) { s_mqttApplyPending.store(pending, std::memory_order_release); }

bool mqttCfgApplyPending() { return s_mqttApplyPending.load(std::memory_order_acquire); }

void mqttCfgSnapshot(MqttConfig *out) {
    if (out == nullptr) {
        return;
    }
    mqttCfgLock();
    *out = mqttCfg;
    mqttCfgUnlock();
}

bool mqttCfgSnapshotTimed(MqttConfig *out, uint32_t timeoutMs) {
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

bool mqttCfgEquals(const MqttConfig *a, const MqttConfig *b) {
    if (a == nullptr || b == nullptr) {
        return false;
    }
    return memcmp(a, b, sizeof(MqttConfig)) == 0;
}

bool mqttCfgIsBrokerConfigured() { return s_brokerConfigured.load(std::memory_order_acquire); }

bool mqttCfgIsPaired() { return s_paired.load(std::memory_order_acquire); }

bool mqttCfgIsHeartReady() {
    return s_brokerConfigured.load(std::memory_order_acquire) && s_paired.load(std::memory_order_acquire);
}

void mqttCfgTopicPubLockedCopy(char *out, size_t outLen) {
    if (out == nullptr || outLen == 0U) {
        return;
    }
    mqttCfgLock();
    strlcpy(out, mqttCfg.topicPub, outLen);
    mqttCfgUnlock();
}

void mqttCfgStorePending(const MqttConfig *pending) {
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
    mqttCfgRefreshFlagsLocked();
    mqttCfgUnlock();
    mqttCfgMarkDirty();
}

void mqttCfgPendingSnapshot(MqttConfig *out) {
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

/** Read broker fields from an open Preferences handle into `out` (raw port, not normalized). */
static void mqttCfgReadFromPrefs(Preferences &prefs, MqttConfig &out) {
    prefs.getString(kNvsKeyMqttServer, out.server, sizeof(out.server));
    out.port = static_cast<uint16_t>(prefs.getInt(kNvsKeyMqttPort, static_cast<int>(kMqttDefaultTlsPort)));
    // Missing key → TLS (backward compatible with pre-tls-flag firmware).
    out.tls = prefs.getUChar(kNvsKeyMqttTls, 1U) != 0U;
    prefs.getString(kNvsKeyMqttUser, out.username, sizeof(out.username));
    prefs.getString(kNvsKeyMqttPass, out.password, sizeof(out.password));
    prefs.getString(kNvsKeyMqttPartnerId, out.partnerDeviceId, sizeof(out.partnerDeviceId));
}

bool mqttCfgMatchesNvs() {
    MqttConfig active{};
    mqttCfgSnapshot(&active);

    MqttConfig stored{};
    bool nvsPresent = false;
    {
        app_nvs::ScopedNvsLock lock;
        Preferences prefs;
        if (!prefs.begin(kNvsNsMqtt, true)) {
            return active.server[0] == '\0';
        }
        nvsPresent = true;
        mqttCfgReadFromPrefs(prefs, stored);
        prefs.end();
    }

    // Sanitize outside the NVS lock: buildDeviceId() also takes g_nvsMutex.
    if (nvsPresent) {
        mqttCfgSanitizeAfterNvsLoad(stored);
    }
    return memcmp(&active, &stored, sizeof(MqttConfig)) == 0;
}

void loadMQTTConfig() {
    MqttConfig loaded{};

    {
        app_nvs::ScopedNvsLock lock;
        Preferences prefs;
        if (!prefs.begin(kNvsNsMqtt, true)) {
            ESP_LOGI(TAG, "NVS mqtt namespace not present, using MQTT defaults");
            loaded.server[0] = '\0';
            loaded.username[0] = '\0';
            loaded.password[0] = '\0';
            loaded.port = kMqttDefaultTlsPort;
            loaded.tls = true;
        } else if (!prefs.isKey(kNvsKeyMqttServer)) {
            ESP_LOGI(TAG, "MQTT not configured yet in NVS, using defaults");
            loaded.server[0] = '\0';
            loaded.tls = true;
            prefs.end();
        } else {
            mqttCfgReadFromPrefs(prefs, loaded);
            loaded.port = normalizeMqttPort(static_cast<int>(loaded.port));
            prefs.end();
        }
    }

    // Sanitize outside the NVS lock: buildDeviceId() also takes g_nvsMutex (non-recursive).
    mqttCfgSanitizeAfterNvsLoad(loaded);
    mqttCfgLock();
    mqttCfg = loaded;
    s_mqttPendingCfg = loaded;
    mqttCfgRefreshFlagsLocked();
    mqttCfgUnlock();
    mqttCfgMarkDirty();
}

// User/password are stored as plain Preferences strings in NVS.
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
    ok = (prefs.putUChar(kNvsKeyMqttTls, snap.tls ? 1U : 0U) > 0);
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
    // Topics are derived from device/partner IDs — remove legacy NVS keys if present.
    if (prefs.isKey(kNvsKeyMqttTopicPub)) {
        static_cast<void>(prefs.remove(kNvsKeyMqttTopicPub));
    }
    if (prefs.isKey(kNvsKeyMqttTopicSub)) {
        static_cast<void>(prefs.remove(kNvsKeyMqttTopicSub));
    }
    ok = mqttCfgPutStringOrEmpty(prefs, kNvsKeyMqttPartnerId, snap.partnerDeviceId);
    prefs.end();
    if (!ok) {
        ESP_LOGE(TAG, "NVS mqtt: persist failed");
        return false;
    }
    return true;
}
