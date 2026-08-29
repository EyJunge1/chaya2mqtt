#pragma once

#include <cstdint>

#include "constants.h"
#include "mqtt_config.h"

struct MqttConfig {
    char     server[128] = "";
    uint16_t port        = kMqttDefaultTlsPort;
    /** true = mqtts (TLS), false = mqtt (plain TCP). Default TLS for backward compatibility. */
    bool     tls         = true;
    char     username[64] = "";
    char     password[64] = "";
    /** Derived: chaya2mqtt/<own device id>. Not user-editable. */
    char     topicPub[128] = "";
    /** Derived: chaya2mqtt/<partner id>, or empty when unpaired. Not user-editable. */
    char     topicSub[128] = "";
    char     partnerDeviceId[kDeviceIdBufLen] = "";
};

/** Derive topicPub/topicSub from own + partner IDs (empty topicSub when unpaired). */
void mqttCfgApplyPairingTopics(MqttConfig* cfg);

/** Active broker config lives in mqtt/config.cpp — use mqttCfgSnapshot / mqttCfgStorePending /
 *  mqttCfgApplyPendingToActive / mqttCfgTopicPubLockedCopy only (FreeRTOS mutex, not ISR-safe).
 */
void loadMQTTConfig();
bool saveMQTTConfig();

void mqttCfgSnapshot(MqttConfig* out);
bool mqttCfgIsBrokerConfigured();
/** True when a partner device ID is set (non-empty after sanitization). */
bool mqttCfgIsPaired();
/** True when broker and partner are set — ready for the operational heart view. */
bool mqttCfgIsHeartReady();
void mqttCfgTopicPubLockedCopy(char* out, size_t outLen);

void mqttCfgStorePending(const MqttConfig* pending);
void mqttCfgApplyPendingToActive();
bool mqttCfgConsumeDirtySnapshotNeeded();

/** Pending form snapshot (web POST before network task applies). */
void mqttCfgPendingSnapshot(MqttConfig* out);

/** True when pending differs from active (saved banner still applying). */
bool mqttCfgHasUnappliedPending();

/** True when active in-RAM config matches persisted NVS (no write needed). */
bool mqttCfgMatchesNvs();

/** True when two config snapshots are identical. */
bool mqttCfgEquals(const MqttConfig* a, const MqttConfig* b);

/** Snapshot with bounded wait; false if cfg mutex unavailable. */
bool mqttCfgSnapshotTimed(MqttConfig* out, uint32_t timeoutMs);
