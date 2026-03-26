#include "config.h"

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
char mqtt_topic[128] = "esp32/heart_counter";

static WiFiManagerParameter* g_param_mqtt_server = nullptr;
static WiFiManagerParameter* g_param_mqtt_port = nullptr;
static WiFiManagerParameter* g_param_mqtt_user = nullptr;
static WiFiManagerParameter* g_param_mqtt_pass = nullptr;
static WiFiManagerParameter* g_param_mqtt_topic = nullptr;

static void safeStrCopy(char* dst, size_t dstSize, const char* src) {
    if (!dst || dstSize == 0) return;
    strncpy(dst, src, dstSize - 1);
    dst[dstSize - 1] = '\0';
}

void loadMQTTConfig() {
    preferences.begin("mqtt", true);
    safeStrCopy(mqtt_server, sizeof(mqtt_server), preferences.getString("server", "").c_str());
    mqtt_port = preferences.getInt("port", 8883);
    safeStrCopy(mqtt_username, sizeof(mqtt_username), preferences.getString("user", "").c_str());
    safeStrCopy(mqtt_password, sizeof(mqtt_password), preferences.getString("pass", "").c_str());
    safeStrCopy(mqtt_topic, sizeof(mqtt_topic),
                preferences.getString("topic", "esp32/heart_counter").c_str());
    preferences.end();
}

void saveMQTTConfig() {
    preferences.begin("mqtt", false);
    preferences.putString("server", mqtt_server);
    preferences.putInt("port", mqtt_port);
    preferences.putString("user", mqtt_username);
    preferences.putString("pass", mqtt_password);
    preferences.putString("topic", mqtt_topic);
    preferences.end();
}

static void saveParamsFromPortal() {
    if (g_param_mqtt_server) {
        safeStrCopy(mqtt_server, sizeof(mqtt_server), g_param_mqtt_server->getValue());
    }
    if (g_param_mqtt_port) {
        mqtt_port = atoi(g_param_mqtt_port->getValue());
        if (mqtt_port <= 0 || mqtt_port > 65535) {
            mqtt_port = 8883;
        }
    }
    if (g_param_mqtt_user) {
        safeStrCopy(mqtt_username, sizeof(mqtt_username), g_param_mqtt_user->getValue());
    }
    if (g_param_mqtt_pass) {
        safeStrCopy(mqtt_password, sizeof(mqtt_password), g_param_mqtt_pass->getValue());
    }
    if (g_param_mqtt_topic) {
        safeStrCopy(mqtt_topic, sizeof(mqtt_topic), g_param_mqtt_topic->getValue());
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
    WiFiManagerParameter param_topic("mqtt_topic", "MQTT Topic", mqtt_topic, 128);

    wifiManager.addParameter(&param_server);
    wifiManager.addParameter(&param_port);
    wifiManager.addParameter(&param_user);
    wifiManager.addParameter(&param_pass);
    wifiManager.addParameter(&param_topic);

    g_param_mqtt_server = &param_server;
    g_param_mqtt_port = &param_port;
    g_param_mqtt_user = &param_user;
    g_param_mqtt_pass = &param_pass;
    g_param_mqtt_topic = &param_topic;

    wifiManager.setSaveParamsCallback(saveParamsFromPortal);

    Serial.print("Starte WiFi (Captive Portal bei Bedarf: HeartESP32-Setup)...");
    if (!wifiManager.autoConnect("HeartESP32-Setup")) {
        Serial.println("Konfiguration fehlgeschlagen, Neustart...");
        g_param_mqtt_server = nullptr;
        g_param_mqtt_port = nullptr;
        g_param_mqtt_user = nullptr;
        g_param_mqtt_pass = nullptr;
        g_param_mqtt_topic = nullptr;
        delay(3000);
        ESP.restart();
    }

    g_param_mqtt_server = nullptr;
    g_param_mqtt_port = nullptr;
    g_param_mqtt_user = nullptr;
    g_param_mqtt_pass = nullptr;
    g_param_mqtt_topic = nullptr;

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
    delay(500);
    ESP.restart();
}
