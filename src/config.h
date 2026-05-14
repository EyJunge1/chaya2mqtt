#pragma once

#include <cstdint>

struct MqttConfig {
    char server[128]    = "";
    uint16_t port       = 8883;
    char username[64]   = "";
    char password[64]   = "";
    char topicPub[128]  = "chaya/to_b";
    char topicSub[128]  = "chaya/to_a";
};

extern MqttConfig mqttCfg;

void loadMQTTConfig();
void saveMQTTConfig();
