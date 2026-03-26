#pragma once

void mqttSetup();
void mqttLoop();

/** Sendet festen Payload "heart" auf mqtt_topic_pub (TLS); mehrere Versuche mit client.loop(). */
bool mqttPublishHeart();
