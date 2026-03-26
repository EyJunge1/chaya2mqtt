#pragma once

#include <PubSubClient.h>
#include <WiFiClientSecure.h>

extern WiFiClientSecure espClient;
extern PubSubClient client;

void mqttSetup();
void mqttLoop();

/** Sendet festen Payload "heart" auf mqtt_topic_pub (TLS). */
bool mqttPublishHeart();
