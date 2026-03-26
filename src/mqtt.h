#pragma once

#include <PubSubClient.h>
#include <WiFiClientSecure.h>

extern WiFiClientSecure espClient;
extern PubSubClient client;

void mqttSetup();
void mqttReconnect();
void mqttLoop();
