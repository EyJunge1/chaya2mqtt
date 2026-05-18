#pragma once

#include <cstdint>

#include "constants.h"

struct MqttConfig {
    char     server[128] = "";
    uint16_t port        = kMqttDefaultTlsPort;
    char     username[64] = "";
    char     password[64] = "";
    char     topicPub[128] = "chaya/to_b";
    char     topicSub[128] = "chaya/to_a";
};

extern MqttConfig mqttCfg;

void loadMQTTConfig();
bool saveMQTTConfig();

void mqttCfgSnapshot(MqttConfig* out);
bool mqttCfgIsBrokerConfigured();
void mqttCfgTopicPubLockedCopy(char* out, size_t outLen);

void mqttCfgStorePending(const MqttConfig* pending);
void mqttCfgApplyPendingToActive();
bool mqttCfgConsumeDirtySnapshotNeeded();
