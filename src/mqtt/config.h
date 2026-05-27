#pragma once

#include <cstdint>

#include "constants.h"
#include "mqtt_config.h"

struct MqttConfig {
    char     server[128] = "";
    uint16_t port        = kMqttDefaultTlsPort;
    char     username[64] = "";
    char     password[64] = "";
    char     topicPub[128] = "chaya/to_b";
    char     topicSub[128] = "chaya/to_a";
    char     partnerDeviceId[kDeviceIdBufLen] = "";
};

/** Build this device's 6-char lowercase hex ID from the ESP32 MAC (last 3 bytes). */
void buildDeviceId(char* out, size_t outLen);

/** When partnerDeviceId is set, overwrite topicPub/topicSub from own + partner IDs. */
void mqttCfgApplyPairingTopics(MqttConfig* cfg);

/** Active broker config lives in mqtt/config.cpp — use mqttCfgSnapshot / mqttCfgStorePending /
 *  mqttCfgApplyPendingToActive / mqttCfgTopicPubLockedCopy only (FreeRTOS mutex, not ISR-safe).
 */
void loadMQTTConfig();
bool saveMQTTConfig();

void mqttCfgSnapshot(MqttConfig* out);
bool mqttCfgIsBrokerConfigured();
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
