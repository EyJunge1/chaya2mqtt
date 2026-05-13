#include "config.h"

#include "mqtt.h"
#include "web_admin.h"

#include <Arduino.h>
#include <MycilaESPConnect.h>
#include <Preferences.h>
#include <WiFi.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <esp_log.h>
#include <esp_wifi.h>

#if defined(CORE_DEBUG_LEVEL) && CORE_DEBUG_LEVEL > 0
static const char* TAG = "CFG";
#else
static constexpr const char* TAG __attribute__((unused)) = "";
#endif

// ─── Globale MQTT-Konfiguration ───────────────────────────────────────────────

MqttConfig mqttCfg;

// ─── Herz-Zähler ──────────────────────────────────────────────────────────────

int heartCounter = 0;

static int           lastCommittedHeartCounter            = 0;
static unsigned long lastHeartCounterSaveMs               = 0;
static constexpr unsigned long kHeartCounterSaveMinIntervalMs = 30000;

// ─── WiFi / Portal (MycilaESPConnect) ─────────────────────────────────────────

static Preferences preferences;

static constexpr char kDeviceHostname[]  = "chaya2mqtt";
static constexpr char kSetupApSsid[]   = "chaya2mqtt-Setup";

/** Erste Nutzung konstruiert ESPConnect mit gemeinsamem AsyncWebServer (siehe web_admin). */
static Mycila::ESPConnect& espConnectInstance() {
    static Mycila::ESPConnect instance(webAdminWebServer());
    return instance;
}

// ─── Hilfsfunktionen ──────────────────────────────────────────────────────────

static void loadWifiIntoConfig(Mycila::ESPConnect::Config& cfg) {
    if (!preferences.begin("wifi", true)) {
        return;
    }
    const String ssid = preferences.getString("ssid", "");
    const String pass = preferences.getString("pass", "");
    preferences.end();
    // ESPConnect nutzt std::string: c_str() kopiert sofort in eigenen Puffer (kein Hänger wie bei const char*-Alias).
    cfg.wifiSSID     = ssid.c_str();
    cfg.wifiPassword = pass.c_str();
}

static void saveWifiFromConfig(const Mycila::ESPConnect::Config& cfg) {
    if (!preferences.begin("wifi", false)) {
        ESP_LOGE(TAG, "NVS wifi: schreiben fehlgeschlagen (Portal)");
        return;
    }
    preferences.putString("ssid", cfg.wifiSSID.c_str());
    preferences.putString("pass", cfg.wifiPassword.c_str());
    preferences.end();
}

/** STA hat IP oder Gerät bleibt absichtlich nur im SoftAP-Modus. */
static bool wifiSetupGoalReached(const Mycila::ESPConnect& ec) {
    using S = Mycila::ESPConnect::State;
    const S st = ec.getState();
    if (ec.getConfig().apMode && st == S::AP_STARTED) {
        return true;
    }
    return WiFi.status() == WL_CONNECTED && WiFi.localIP()[0] != 0;
}

// ─── NVS: MQTT ────────────────────────────────────────────────────────────────

void loadMQTTConfig() {
    if (!preferences.begin("mqtt", true)) {
        ESP_LOGW(TAG, "NVS mqtt: lesen fehlgeschlagen, nutze Defaults");
        return;
    }
    preferences.getString("server", mqttCfg.server, sizeof(mqttCfg.server));
    const int p = preferences.getInt("port", 8883);
    mqttCfg.port   = (p > 0 && p <= 65535) ? static_cast<uint16_t>(p) : 8883;
    preferences.getString("user", mqttCfg.username, sizeof(mqttCfg.username));
    preferences.getString("pass", mqttCfg.password, sizeof(mqttCfg.password));
    if (preferences.getString("topic_pub", mqttCfg.topicPub, sizeof(mqttCfg.topicPub)) == 0
        || mqttCfg.topicPub[0] == '\0') {
        strlcpy(mqttCfg.topicPub, "heart/to_b", sizeof(mqttCfg.topicPub));
    }
    if (preferences.getString("topic_sub", mqttCfg.topicSub, sizeof(mqttCfg.topicSub)) == 0
        || mqttCfg.topicSub[0] == '\0') {
        strlcpy(mqttCfg.topicSub, "heart/to_a", sizeof(mqttCfg.topicSub));
    }
    preferences.end();
}

void saveMQTTConfig() {
    if (!preferences.begin("mqtt", false)) {
        ESP_LOGE(TAG, "NVS mqtt: schreiben fehlgeschlagen");
        return;
    }
    preferences.putString("server", mqttCfg.server);
    preferences.putInt("port", mqttCfg.port);
    preferences.putString("user", mqttCfg.username);
    preferences.putString("pass", mqttCfg.password);
    preferences.putString("topic_pub", mqttCfg.topicPub);
    preferences.putString("topic_sub", mqttCfg.topicSub);
    preferences.end();
}

// ─── NVS: Herz-Zähler ─────────────────────────────────────────────────────────

void loadHeartCounter() {
    if (!preferences.begin("heart", true)) {
        ESP_LOGW(TAG, "NVS heart: lesen fehlgeschlagen, Zaehler = 0");
        heartCounter              = 0;
        lastCommittedHeartCounter = 0;
        lastHeartCounterSaveMs    = millis();
        return;
    }
    heartCounter = std::max<int32_t>(preferences.getInt("counter", 0), 0);
    preferences.end();
    lastCommittedHeartCounter = heartCounter;
    lastHeartCounterSaveMs    = millis();
}

bool saveHeartCounter() {
    if (!preferences.begin("heart", false)) {
        ESP_LOGE(TAG, "NVS heart: schreiben fehlgeschlagen");
        return false;
    }
    preferences.putInt("counter", heartCounter);
    preferences.end();
    return true;
}

void maybeSaveHeartCounter() {
    if (heartCounter == lastCommittedHeartCounter) {
        return;
    }
    const unsigned long now = millis();
    if (now - lastHeartCounterSaveMs >= kHeartCounterSaveMinIntervalMs) {
        if (saveHeartCounter()) {
            lastCommittedHeartCounter = heartCounter;
            lastHeartCounterSaveMs    = now;
        }
    }
}

void flushHeartCounterIfDirty() {
    if (heartCounter != lastCommittedHeartCounter) {
        if (saveHeartCounter()) {
            lastCommittedHeartCounter = heartCounter;
            lastHeartCounterSaveMs    = millis();
        }
    }
}

// ─── WiFi-Setup ───────────────────────────────────────────────────────────────

void setupWiFi() {
    webAdminRegisterMqttRoutes();

    Mycila::ESPConnect::Config cfg = {};
    cfg.hostname = kDeviceHostname;
    cfg.apMode   = false;
    loadWifiIntoConfig(cfg);

    Mycila::ESPConnect& ec = espConnectInstance();
    ec.listen([&ec](Mycila::ESPConnect::State /*previous*/, Mycila::ESPConnect::State state) {
        if (state == Mycila::ESPConnect::State::PORTAL_COMPLETE) {
            const Mycila::ESPConnect::Config& c = ec.getConfig();
            if (!c.apMode) {
                saveWifiFromConfig(c);
            }
            flushHeartCounterIfDirty();
        }
    });

    ec.setAutoRestart(true);
    // Blocking true wartet nicht auf PORTAL_STARTED → Deadlock im Captive Portal.
    ec.setBlocking(false);
    ec.begin(kSetupApSsid, "", cfg);

    ESP_LOGI(TAG, "ESPConnect gestartet (non-blocking), warte auf STA oder Neustart...");
    while (!wifiSetupGoalReached(ec)) {
        ec.loop();
        delay(10);
        // Nach erfolgreichem Portal folgt i.d.R. ESP.restart(); diese Schleife endet dann dort.
    }

    WiFi.setSleep(true);
    esp_wifi_set_ps(WIFI_PS_MAX_MODEM);
    (void)esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20);
    esp_wifi_set_max_tx_power(52);

    ESP_LOGI(TAG, "WLAN bereit, STA-IP: %s", WiFi.localIP().toString().c_str());
}

// ─── Factory Reset ────────────────────────────────────────────────────────────

void resetAllSettings() {
    ESP_LOGW(TAG, "Factory Reset: alle Einstellungen loeschen...");
    webAdminStopMaintenanceHttp();
    Mycila::ESPConnect& ec = espConnectInstance();
    ec.clearConfiguration();
    ec.end();
    WiFi.disconnect(true, true);
    if (preferences.begin("wifi", false)) {
        preferences.clear();
        preferences.end();
    }
    if (preferences.begin("mqtt", false)) {
        preferences.clear();
        preferences.end();
    }
    if (preferences.begin("cfg", false)) {
        preferences.clear();
        preferences.end();
    }
    flushHeartCounterIfDirty();
    delay(500);
    ESP.restart();
}

// ─── Loop ─────────────────────────────────────────────────────────────────────

void configLoop() {
    Mycila::ESPConnect& ec = espConnectInstance();
    ec.loop();
    webAdminMaybeStopMaintenanceIfBrokerConfigured();
    webAdminMaybeStartMaintenance(ec);
}

bool configIsSetupPortalActive() {
    using S = Mycila::ESPConnect::State;
    const S st = espConnectInstance().getState();
    if (st == S::PORTAL_STARTING || st == S::PORTAL_STARTED) {
        return true;
    }
    return webAdminIsMaintenanceHttpActive();
}

Mycila::ESPConnect& configEspConnect() {
    return espConnectInstance();
}
