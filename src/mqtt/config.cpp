#include "config.h"

#include "mqtt_pack.h"
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

void mqttCfgResetRamAfterFactoryClear() {
    mqttCfgLock();
    mqttCfg = MqttConfig{};
    s_mqttPendingCfg = MqttConfig{};
    mqttCfgRefreshFlagsLocked();
    mqttCfgUnlock();
    s_mqttApplyPending.store(false, std::memory_order_release);
    s_mqttNvsWriteFailed.store(false, std::memory_order_release);
    mqttCfgMarkDirty();
}

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

bool mqttCfgSnapshotWithApplyFlagsTimed(MqttConfig *out, bool *applyPending, bool *nvsOk, uint32_t timeoutMs) {
    if (out == nullptr || applyPending == nullptr || nvsOk == nullptr) {
        return false;
    }
    mqttCfgMutexEnsureCreated();
    if (xSemaphoreTake(s_mqttCfgMutex, pdMS_TO_TICKS(timeoutMs)) != pdTRUE) {
        return false;
    }
    *out = mqttCfg;
    // Apply-to-active updates mqttCfg under this lock; pending/nvs flags are cleared after
    // that release (mqttFinishSettingsApply / saveMQTTConfig). Same-hold reads cannot see
    // stale cfg with applyPending==false.
    *applyPending = s_mqttApplyPending.load(std::memory_order_acquire);
    *nvsOk = !s_mqttNvsWriteFailed.load(std::memory_order_acquire);
    xSemaphoreGive(s_mqttCfgMutex);
    return true;
}

bool mqttCfgPendingSnapshotTimed(MqttConfig *out, uint32_t timeoutMs) {
    if (out == nullptr) {
        return false;
    }
    mqttCfgMutexEnsureCreated();
    if (xSemaphoreTake(s_mqttCfgMutex, pdMS_TO_TICKS(timeoutMs)) != pdTRUE) {
        return false;
    }
    *out = s_mqttPendingCfg;
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

bool mqttCfgStorePendingTimed(const MqttConfig *pending, uint32_t timeoutMs) {
    if (pending == nullptr) {
        return false;
    }
    mqttCfgMutexEnsureCreated();
    if (xSemaphoreTake(s_mqttCfgMutex, pdMS_TO_TICKS(timeoutMs)) != pdTRUE) {
        return false;
    }
    s_mqttPendingCfg = *pending;
    xSemaphoreGive(s_mqttCfgMutex);
    return true;
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

static bool mqttCfgTryReadBlob(Preferences &prefs, MqttConfig &out) {
    if (prefs.getBytesLength(kNvsKeyMqttCfgV1) != sizeof(PackedMqttConfigV1)) {
        return false;
    }
    PackedMqttConfigV1 pk{};
    if (prefs.getBytes(kNvsKeyMqttCfgV1, &pk, sizeof(pk)) != sizeof(pk)) {
        return false;
    }
    return mqttUnpackConfigV1(pk, &out);
}

static bool mqttCfgLegacyKeysPresent(Preferences &prefs) {
    return prefs.isKey(kNvsKeyMqttServer) || prefs.isKey(kNvsKeyMqttPort) || prefs.isKey(kNvsKeyMqttTls) ||
           prefs.isKey(kNvsKeyMqttUser) || prefs.isKey(kNvsKeyMqttPass) || prefs.isKey(kNvsKeyMqttPartnerId);
}

static void mqttCfgRemoveLegacyKeys(Preferences &prefs) {
    static_cast<void>(prefs.remove(kNvsKeyMqttServer));
    static_cast<void>(prefs.remove(kNvsKeyMqttPort));
    static_cast<void>(prefs.remove(kNvsKeyMqttTls));
    static_cast<void>(prefs.remove(kNvsKeyMqttUser));
    static_cast<void>(prefs.remove(kNvsKeyMqttPass));
    static_cast<void>(prefs.remove(kNvsKeyMqttTopicPub));
    static_cast<void>(prefs.remove(kNvsKeyMqttTopicSub));
    static_cast<void>(prefs.remove(kNvsKeyMqttPartnerId));
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
        if (!mqttCfgTryReadBlob(prefs, stored)) {
            mqttCfgReadFromPrefs(prefs, stored);
            stored.port = normalizeMqttPort(static_cast<int>(stored.port));
        }
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
    bool migrateLegacy = false;

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
        } else if (mqttCfgTryReadBlob(prefs, loaded)) {
            ESP_LOGD(TAG, "MQTT NVS: cfg_v1 loaded");
            prefs.end();
        } else if (mqttCfgLegacyKeysPresent(prefs)) {
            ESP_LOGI(TAG, "MQTT NVS: migrating legacy keys to cfg_v1");
            mqttCfgReadFromPrefs(prefs, loaded);
            loaded.port = normalizeMqttPort(static_cast<int>(loaded.port));
            migrateLegacy = true;
            prefs.end();
        } else {
            ESP_LOGI(TAG, "MQTT not configured yet in NVS, using defaults");
            loaded.server[0] = '\0';
            loaded.tls = true;
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
    if (migrateLegacy && !saveMQTTConfig()) {
        ESP_LOGW(TAG, "MQTT NVS: cfg_v1 migrate write failed");
    }
    mqttCfgMarkDirty();
}

bool saveMQTTConfig() {
    if (app_nvs::writesBlocked(kNvsNsMqtt)) {
        ESP_LOGW(TAG, "NVS mqtt: save blocked during shutdown");
        return false;
    }
    MqttConfig snap{};
    mqttCfgSnapshot(&snap);

    PackedMqttConfigV1 pk{};
    mqttPackConfigV1(snap, &pk);

    app_nvs::ScopedNvsWriteLock lock(kNvsNsMqtt);
    if (!lock) {
        ESP_LOGW(TAG, "NVS mqtt: save blocked during shutdown");
        return false;
    }
    Preferences prefs;
    if (!prefs.begin(kNvsNsMqtt, false)) {
        ESP_LOGE(TAG, "NVS mqtt: begin failed");
        return false;
    }
    const size_t w = prefs.putBytes(kNvsKeyMqttCfgV1, &pk, sizeof(pk));
    if (w != sizeof(pk)) {
        prefs.end();
        ESP_LOGE(TAG, "NVS mqtt: cfg_v1 persist failed");
        return false;
    }
    mqttCfgRemoveLegacyKeys(prefs);
    prefs.end();
    return true;
}
