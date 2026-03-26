#include "config.h"

#include "display.h"

#include <WiFi.h>
#include <WiFiManager.h>
#include <Arduino.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

Preferences preferences;
char mqtt_server[128] = "";
int mqtt_port = 8883;
char mqtt_username[64] = "";
char mqtt_password[64] = "";
char mqtt_topic_pub[128] = "heart/to_b";
char mqtt_topic_sub[128] = "heart/to_a";

static int lastCommittedHeartCounter = 0;
static unsigned long lastHeartCounterSaveMs = 0;
static constexpr unsigned long kHeartCounterSaveMinIntervalMs = 30000;

static WiFiManagerParameter* g_param_mqtt_server = nullptr;
static WiFiManagerParameter* g_param_mqtt_port = nullptr;
static WiFiManagerParameter* g_param_mqtt_user = nullptr;
static WiFiManagerParameter* g_param_mqtt_pass = nullptr;
static WiFiManagerParameter* g_param_mqtt_topic_pub = nullptr;
static WiFiManagerParameter* g_param_mqtt_topic_sub = nullptr;

static void safeStrCopy(char* dst, size_t dstSize, const char* src) {
    if (dst == nullptr || dstSize == 0) {
        return;
    }
    strncpy(dst, src, dstSize - 1);
    dst[dstSize - 1] = '\0';
}

void loadMQTTConfig() {
    preferences.begin("mqtt", true);
    safeStrCopy(mqtt_server, sizeof(mqtt_server), preferences.getString("server", "").c_str());
    mqtt_port = preferences.getInt("port", 8883);
    safeStrCopy(mqtt_username, sizeof(mqtt_username), preferences.getString("user", "").c_str());
    safeStrCopy(mqtt_password, sizeof(mqtt_password), preferences.getString("pass", "").c_str());
    safeStrCopy(mqtt_topic_pub, sizeof(mqtt_topic_pub),
                preferences.getString("topic_pub", "heart/to_b").c_str());
    safeStrCopy(mqtt_topic_sub, sizeof(mqtt_topic_sub),
                preferences.getString("topic_sub", "heart/to_a").c_str());
    preferences.end();
}

void loadHeartCounter() {
    preferences.begin("heart", true);
    counter = preferences.getInt("counter", 0);
    preferences.end();
    lastCommittedHeartCounter = counter;
    lastHeartCounterSaveMs = millis();
}

void saveHeartCounter() {
    preferences.begin("heart", false);
    preferences.putInt("counter", counter);
    preferences.end();
}

void maybeSaveHeartCounter() {
    if (counter == lastCommittedHeartCounter) {
        return;
    }
    const unsigned long now = millis();
    if (now - lastHeartCounterSaveMs >= kHeartCounterSaveMinIntervalMs) {
        saveHeartCounter();
        lastCommittedHeartCounter = counter;
        lastHeartCounterSaveMs = now;
    }
}

void flushHeartCounterIfDirty() {
    if (counter != lastCommittedHeartCounter) {
        saveHeartCounter();
        lastCommittedHeartCounter = counter;
        lastHeartCounterSaveMs = millis();
    }
}

void saveMQTTConfig() {
    preferences.begin("mqtt", false);
    preferences.putString("server", mqtt_server);
    preferences.putInt("port", mqtt_port);
    preferences.putString("user", mqtt_username);
    preferences.putString("pass", mqtt_password);
    preferences.putString("topic_pub", mqtt_topic_pub);
    preferences.putString("topic_sub", mqtt_topic_sub);
    preferences.end();
}

static void saveParamsFromPortal() {
    if (g_param_mqtt_server != nullptr) {
        safeStrCopy(mqtt_server, sizeof(mqtt_server), g_param_mqtt_server->getValue());
    }
    if (g_param_mqtt_port != nullptr) {
        mqtt_port = atoi(g_param_mqtt_port->getValue());
        if (mqtt_port <= 0 || mqtt_port > 65535) {
            mqtt_port = 8883;
        }
    }
    if (g_param_mqtt_user != nullptr) {
        safeStrCopy(mqtt_username, sizeof(mqtt_username), g_param_mqtt_user->getValue());
    }
    if (g_param_mqtt_pass != nullptr) {
        safeStrCopy(mqtt_password, sizeof(mqtt_password), g_param_mqtt_pass->getValue());
    }
    if (g_param_mqtt_topic_pub != nullptr) {
        safeStrCopy(mqtt_topic_pub, sizeof(mqtt_topic_pub), g_param_mqtt_topic_pub->getValue());
    }
    if (g_param_mqtt_topic_sub != nullptr) {
        safeStrCopy(mqtt_topic_sub, sizeof(mqtt_topic_sub), g_param_mqtt_topic_sub->getValue());
    }
    saveMQTTConfig();
}

void setupWiFi() {
    WiFiManager wifiManager;
    wifiManager.setConfigPortalTimeout(180);

    char portStr[8];
    snprintf(portStr, sizeof(portStr), "%d", mqtt_port);

    WiFiManagerParameter param_server("mqtt_server", "MQTT Server", mqtt_server, 128);
    WiFiManagerParameter param_port("mqtt_port", "MQTT Port", portStr, 6);
    WiFiManagerParameter param_user("mqtt_user", "MQTT Username", mqtt_username, 64);
    WiFiManagerParameter param_pass("mqtt_pass", "MQTT Password", mqtt_password, 64);
    WiFiManagerParameter param_topic_pub("mqtt_topic_pub", "MQTT Sende-Topic", mqtt_topic_pub, 128);
    WiFiManagerParameter param_topic_sub("mqtt_topic_sub", "MQTT Empfangs-Topic", mqtt_topic_sub, 128);

    wifiManager.addParameter(&param_server);
    wifiManager.addParameter(&param_port);
    wifiManager.addParameter(&param_user);
    wifiManager.addParameter(&param_pass);
    wifiManager.addParameter(&param_topic_pub);
    wifiManager.addParameter(&param_topic_sub);

    g_param_mqtt_server = &param_server;
    g_param_mqtt_port = &param_port;
    g_param_mqtt_user = &param_user;
    g_param_mqtt_pass = &param_pass;
    g_param_mqtt_topic_pub = &param_topic_pub;
    g_param_mqtt_topic_sub = &param_topic_sub;

    wifiManager.setSaveParamsCallback(saveParamsFromPortal);

    Serial.print("Starte WiFi (Captive Portal bei Bedarf: HeartESP32-Setup)...");
    if (!wifiManager.autoConnect("HeartESP32-Setup")) {
        Serial.println("Konfiguration fehlgeschlagen, Neustart...");
        g_param_mqtt_server = nullptr;
        g_param_mqtt_port = nullptr;
        g_param_mqtt_user = nullptr;
        g_param_mqtt_pass = nullptr;
        g_param_mqtt_topic_pub = nullptr;
        g_param_mqtt_topic_sub = nullptr;
        flushHeartCounterIfDirty();
        delay(3000);
        ESP.restart();
    }

    g_param_mqtt_server = nullptr;
    g_param_mqtt_port = nullptr;
    g_param_mqtt_user = nullptr;
    g_param_mqtt_pass = nullptr;
    g_param_mqtt_topic_pub = nullptr;
    g_param_mqtt_topic_sub = nullptr;

    Serial.println("");
    Serial.println("WiFi verbunden!");
    Serial.print("IP Adresse: ");
    Serial.println(WiFi.localIP());
    Serial.print("Gateway: ");
    Serial.println(WiFi.gatewayIP());
    Serial.print("DNS: ");
    Serial.println(WiFi.dnsIP());
}

void resetAllSettings() {
    Serial.println("Alle Einstellungen werden gelöscht (WLAN + MQTT)...");
    WiFiManager wm;
    wm.resetSettings();
    preferences.begin("mqtt", false);
    preferences.clear();
    preferences.end();
    preferences.begin("heart", false);
    preferences.clear();
    preferences.end();
    delay(500);
    ESP.restart();
}
