#pragma once

#include <Preferences.h>

extern Preferences preferences;
extern char mqtt_server[128];
extern int mqtt_port;
extern char mqtt_username[64];
extern char mqtt_password[64];
extern char mqtt_topic_pub[128];
extern char mqtt_topic_sub[128];

void loadMQTTConfig();
void saveMQTTConfig();
void setupWiFi();
void resetAllSettings();
