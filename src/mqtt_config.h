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
void saveMQTTConfig();

/** Thread-safe snapshot of active broker config (for AsyncWeb / MQTT / other tasks). */
void mqttCfgSnapshot(MqttConfig* out);

/** Store pending MQTT form submitted from web handler (same mutex as mqttCfg). */
void mqttCfgStorePending(const MqttConfig* pending);

/** Copy pending form to active mqttCfg (call from main loop only when applying). */
void mqttCfgApplyPendingToActive();
