#pragma once

void mqttSetup();
void mqttLoop();

/** Sendet einen Publish-Versuch "heart" auf mqtt_topic_pub (TLS). */
bool mqttPublishHeart();
