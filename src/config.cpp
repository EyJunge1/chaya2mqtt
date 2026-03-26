#include "config.h"

#include <WiFi.h>
#include <WiFiManager.h>
#include <Arduino.h>
#include <esp_wifi.h>
#include <Preferences.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#if defined(CORE_DEBUG_LEVEL) && CORE_DEBUG_LEVEL > 0
#define CONFIG_DBG_PRINT(x) Serial.print(x)
#define CONFIG_DBG_PRINTLN(x) Serial.println(x)
#else
#define CONFIG_DBG_PRINT(x) ((void)0)
#define CONFIG_DBG_PRINTLN(x) ((void)0)
#endif

static Preferences preferences;
char mqtt_server[128] = "";
int heartCounter = 0;
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
    if (!preferences.begin("mqtt", true)) {
        CONFIG_DBG_PRINTLN("NVS: Namespace mqtt lesen fehlgeschlagen, nutze Defaults.");
        return;
    }

    preferences.getString("server", mqtt_server, sizeof(mqtt_server));
    mqtt_port = preferences.getInt("port", 8883);
    if (mqtt_port <= 0 || mqtt_port > 65535) {
        mqtt_port = 8883;
    }
    preferences.getString("user", mqtt_username, sizeof(mqtt_username));
    preferences.getString("pass", mqtt_password, sizeof(mqtt_password));

    if (preferences.getString("topic_pub", mqtt_topic_pub, sizeof(mqtt_topic_pub)) == 0 ||
        mqtt_topic_pub[0] == '\0') {
        safeStrCopy(mqtt_topic_pub, sizeof(mqtt_topic_pub), "heart/to_b");
    }
    if (preferences.getString("topic_sub", mqtt_topic_sub, sizeof(mqtt_topic_sub)) == 0 ||
        mqtt_topic_sub[0] == '\0') {
        safeStrCopy(mqtt_topic_sub, sizeof(mqtt_topic_sub), "heart/to_a");
    }

    preferences.end();
}

void loadHeartCounter() {
    if (!preferences.begin("heart", true)) {
        CONFIG_DBG_PRINTLN("NVS: Namespace heart lesen fehlgeschlagen, Zaehler = 0.");
        heartCounter = 0;
        lastCommittedHeartCounter = 0;
        lastHeartCounterSaveMs = millis();
        return;
    }
    heartCounter = preferences.getInt("counter", 0);
    preferences.end();
    lastCommittedHeartCounter = heartCounter;
    lastHeartCounterSaveMs = millis();
}

void saveHeartCounter() {
    if (!preferences.begin("heart", false)) {
        CONFIG_DBG_PRINTLN("NVS: Namespace heart schreiben fehlgeschlagen (Zaehler nicht gespeichert).");
        return;
    }
    preferences.putInt("counter", heartCounter);
    preferences.end();
}

void maybeSaveHeartCounter() {
    if (heartCounter == lastCommittedHeartCounter) {
        return;
    }
    const unsigned long now = millis();
    if (now - lastHeartCounterSaveMs >= kHeartCounterSaveMinIntervalMs) {
        saveHeartCounter();
        lastCommittedHeartCounter = heartCounter;
        lastHeartCounterSaveMs = now;
    }
}

void flushHeartCounterIfDirty() {
    if (heartCounter != lastCommittedHeartCounter) {
        saveHeartCounter();
        lastCommittedHeartCounter = heartCounter;
        lastHeartCounterSaveMs = millis();
    }
}

void saveMQTTConfig() {
    if (!preferences.begin("mqtt", false)) {
        CONFIG_DBG_PRINTLN("NVS: Namespace mqtt schreiben fehlgeschlagen.");
        return;
    }
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

    // WiFiManagerParameter leben auf dem Stack; g_param_* zeigen nur waehrend autoConnect() darauf.
    // Der Save-Params-Callback wird nur innerhalb autoConnect() aufgerufen -- danach nullptr setzen.
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

    CONFIG_DBG_PRINT("Starte WiFi (Captive Portal bei Bedarf: HeartESP32-Setup)...");
    if (!wifiManager.autoConnect("HeartESP32-Setup")) {
        CONFIG_DBG_PRINTLN("Konfiguration fehlgeschlagen, Neustart...");
        g_param_mqtt_server = nullptr;
        g_param_mqtt_port = nullptr;
        g_param_mqtt_user = nullptr;
        g_param_mqtt_pass = nullptr;
        g_param_mqtt_topic_pub = nullptr;
        g_param_mqtt_topic_sub = nullptr;
        flushHeartCounterIfDirty();
        delay(500);
        ESP.restart();
    }

    g_param_mqtt_server = nullptr;
    g_param_mqtt_port = nullptr;
    g_param_mqtt_user = nullptr;
    g_param_mqtt_pass = nullptr;
    g_param_mqtt_topic_pub = nullptr;
    g_param_mqtt_topic_sub = nullptr;

    CONFIG_DBG_PRINTLN("");
    CONFIG_DBG_PRINTLN("WiFi verbunden!");
    CONFIG_DBG_PRINT("IP Adresse: ");
    CONFIG_DBG_PRINTLN(WiFi.localIP());
    CONFIG_DBG_PRINT("Gateway: ");
    CONFIG_DBG_PRINTLN(WiFi.gatewayIP());
    CONFIG_DBG_PRINT("DNS: ");
    CONFIG_DBG_PRINTLN(WiFi.dnsIP());

    WiFi.setSleep(true);
    esp_wifi_set_ps(WIFI_PS_MAX_MODEM);
}

void resetAllSettings() {
    CONFIG_DBG_PRINTLN("Alle Einstellungen werden gelöscht (WLAN + MQTT)...");
    WiFiManager wm;
    wm.resetSettings();
    if (preferences.begin("mqtt", false)) {
        preferences.clear();
        preferences.end();
    } else {
        CONFIG_DBG_PRINTLN("NVS: mqtt loeschen fehlgeschlagen.");
    }
    flushHeartCounterIfDirty();
    delay(500);
    ESP.restart();
}
