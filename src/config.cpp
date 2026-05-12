#include "config.h"

#include "mqtt.h"

#include <Arduino.h>
#include <MycilaESPConnect.h>
#include <Preferences.h>
#include <WiFi.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <esp_wifi.h>

#if defined(CORE_DEBUG_LEVEL) && CORE_DEBUG_LEVEL > 0
#define CONFIG_DBG_PRINT(x) Serial.print(x)
#define CONFIG_DBG_PRINTLN(x) Serial.println(x)
#else
#define CONFIG_DBG_PRINT(x) ((void)0)
#define CONFIG_DBG_PRINTLN(x) ((void)0)
#endif

// ─── Globale MQTT-Variablen ───────────────────────────────────────────────────

char     mqtt_server[128]    = "";
uint16_t mqtt_port           = 8883;
char     mqtt_username[64]   = "";
char     mqtt_password[64]   = "";
char     mqtt_topic_pub[128] = "heart/to_b";
char     mqtt_topic_sub[128] = "heart/to_a";

// ─── Herz-Zähler ──────────────────────────────────────────────────────────────

int heartCounter = 0;

static int           lastCommittedHeartCounter            = 0;
static unsigned long lastHeartCounterSaveMs               = 0;
static constexpr unsigned long kHeartCounterSaveMinIntervalMs = 30000;

// ─── WiFi / Portal (MycilaESPConnect) ─────────────────────────────────────────

static Preferences preferences;

static constexpr char kDeviceHostname[]  = "HeartESP32";
static constexpr char kSetupApSsid[]   = "HeartESP32-Setup";
static constexpr char kPortalPrefsNs[] = "cfg";
static constexpr char kPortalBtnKey[]  = "portal_btn";

static AsyncWebServer      g_webServer(80);
static Mycila::ESPConnect  g_espConnect(g_webServer);
static bool                g_mqttMaintenanceHttpActive = false;
static bool                g_mqttRoutesRegistered      = false;

// ─── Hilfsfunktionen ──────────────────────────────────────────────────────────

static void safeStrCopy(char* dst, size_t n, const char* src) {
    if (dst == nullptr || n == 0) {
        return;
    }
    strncpy(dst, src, n - 1);
    dst[n - 1] = '\0';
}

static void appendHtmlEscaped(String& out, const char* s) {
    if (s == nullptr) {
        return;
    }
    for (; *s != '\0'; ++s) {
        switch (*s) {
            case '&': out += F("&amp;"); break;
            case '"': out += F("&quot;"); break;
            case '<': out += F("&lt;"); break;
            case '>': out += F("&gt;"); break;
            default: out += *s; break;
        }
    }
}

static void loadWifiIntoConfig(Mycila::ESPConnect::Config& cfg) {
    if (!preferences.begin("wifi", true)) {
        return;
    }
    const String ssid = preferences.getString("ssid", "");
    const String pass = preferences.getString("pass", "");
    preferences.end();
    cfg.wifiSSID     = ssid.c_str();
    cfg.wifiPassword = pass.c_str();
}

static void saveWifiFromConfig(const Mycila::ESPConnect::Config& cfg) {
    if (!preferences.begin("wifi", false)) {
        CONFIG_DBG_PRINTLN("NVS wifi: schreiben fehlgeschlagen (Portal).");
        return;
    }
    preferences.putString("ssid", cfg.wifiSSID.c_str());
    preferences.putString("pass", cfg.wifiPassword.c_str());
    preferences.end();
}

static bool consumePortalButtonRequest() {
    if (!preferences.begin(kPortalPrefsNs, false)) {
        return false;
    }
    const bool v = preferences.getBool(kPortalBtnKey, false);
    if (v) {
        preferences.remove(kPortalBtnKey);
    }
    preferences.end();
    return v;
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

static void stopMqttMaintenanceHttp() {
    if (!g_mqttMaintenanceHttpActive) {
        return;
    }
    g_webServer.end();
    g_mqttMaintenanceHttpActive = false;
    CONFIG_DBG_PRINTLN("MQTT-Wartungs-HTTP gestoppt.");
}

static String commonCss() {
    return F("<style>*{box-sizing:border-box}"
             "body{font-family:system-ui,sans-serif;margin:0;padding:16px;max-width:560px}"
             "h1{font-size:1.3rem;margin-bottom:4px}"
             "label{display:block;margin:12px 0 4px;font-weight:600}"
             "input{width:100%;padding:8px;border:1px solid #bbb;border-radius:4px;font-size:1rem}"
             "button,a.btn{display:inline-block;margin-top:14px;padding:10px 20px;"
             "background:#c0392b;color:#fff;border:none;border-radius:4px;"
             "text-decoration:none;cursor:pointer;font-size:1rem}"
             ".ok{color:#0a0;font-weight:600;margin:8px 0}"
             ".err{color:#c00;font-weight:600;margin:8px 0}"
             "ul{padding-left:18px}li{margin:6px 0}"
             "nav{margin-top:20px}nav a{margin-right:12px}</style>");
}

static String buildMqttPage(const char* banner) {
    String html;
    html.reserve(2400);
    html += F("<!DOCTYPE html><html lang='de'><head><meta charset='utf-8'>"
              "<meta name='viewport' content='width=device-width,initial-scale=1'>"
              "<title>MQTT</title>");
    html += commonCss();
    html += F("</head><body><h1>MQTT-Einstellungen</h1>");
    if (banner != nullptr && banner[0] != '\0') {
        html += banner;
    }
    html += F("<form method='post' action='/mqtt'>"
              "<label for='srv'>Broker (Hostname oder IP)</label>"
              "<input id='srv' name='mqtt_server' maxlength='127' value='");
    appendHtmlEscaped(html, mqtt_server);
    char portBuf[8];
    snprintf(portBuf, sizeof(portBuf), "%u", static_cast<unsigned>(mqtt_port));
    html += F("'/>"
              "<label for='prt'>Port</label>"
              "<input id='prt' name='mqtt_port' type='number' min='1' max='65535' value='");
    html += portBuf;
    html += F("'/>"
              "<label for='usr'>Benutzername (optional)</label>"
              "<input id='usr' name='mqtt_user' maxlength='63' value='");
    appendHtmlEscaped(html, mqtt_username);
    html += F("'/>"
              "<label for='pw'>Passwort (optional)</label>"
              "<input id='pw' name='mqtt_pass' type='password' maxlength='63' "
              "autocomplete='current-password' value='");
    appendHtmlEscaped(html, mqtt_password);
    html += F("'/>"
              "<label for='tpub'>Sende-Topic</label>"
              "<input id='tpub' name='mqtt_topic_pub' maxlength='127' value='");
    appendHtmlEscaped(html, mqtt_topic_pub);
    html += F("'/>"
              "<label for='tsub'>Empfangs-Topic</label>"
              "<input id='tsub' name='mqtt_topic_sub' maxlength='127' value='");
    appendHtmlEscaped(html, mqtt_topic_sub);
    html += F("'/>"
              "<button type='submit'>Speichern</button></form>"
              "<nav><a class='btn' href='/heart-setup-exit'>Wartungsseite beenden</a></nav>"
              "</body></html>");
    return html;
}

static void handleMqttPost(AsyncWebServerRequest* req) {
    if (req->hasParam("mqtt_server", true)) {
        safeStrCopy(mqtt_server, sizeof(mqtt_server),
                    req->getParam("mqtt_server", true)->value().c_str());
    }
    if (req->hasParam("mqtt_port", true)) {
        const int p = atoi(req->getParam("mqtt_port", true)->value().c_str());
        mqtt_port   = (p > 0 && p <= 65535) ? static_cast<uint16_t>(p) : 8883;
    }
    if (req->hasParam("mqtt_user", true)) {
        safeStrCopy(mqtt_username, sizeof(mqtt_username),
                    req->getParam("mqtt_user", true)->value().c_str());
    }
    if (req->hasParam("mqtt_pass", true)) {
        safeStrCopy(mqtt_password, sizeof(mqtt_password),
                    req->getParam("mqtt_pass", true)->value().c_str());
    }
    if (req->hasParam("mqtt_topic_pub", true)) {
        safeStrCopy(mqtt_topic_pub, sizeof(mqtt_topic_pub),
                    req->getParam("mqtt_topic_pub", true)->value().c_str());
    }
    if (req->hasParam("mqtt_topic_sub", true)) {
        safeStrCopy(mqtt_topic_sub, sizeof(mqtt_topic_sub),
                    req->getParam("mqtt_topic_sub", true)->value().c_str());
    }
    if (strcmp(mqtt_topic_pub, mqtt_topic_sub) == 0) {
        CONFIG_DBG_PRINTLN("MQTT: Pub/Sub-Topic identisch, setze Defaults.");
        safeStrCopy(mqtt_topic_pub, sizeof(mqtt_topic_pub), "heart/to_b");
        safeStrCopy(mqtt_topic_sub, sizeof(mqtt_topic_sub), "heart/to_a");
    }
    saveMQTTConfig();
    mqttSetup();
    req->redirect(F("/mqtt?saved=1"));
}

static void registerMqttRoutes() {
    if (g_mqttRoutesRegistered) {
        return;
    }
    g_mqttRoutesRegistered = true;

    g_webServer.on("/mqtt", HTTP_GET, [](AsyncWebServerRequest* req) {
        String banner;
        if (req->hasParam("saved")) {
            banner = F("<p class='ok'>&#10003; Gespeichert. MQTT wird neu verbunden.</p>");
        }
        req->send(200, F("text/html"), buildMqttPage(banner.c_str()));
    });
    g_webServer.on("/mqtt", HTTP_POST, handleMqttPost);

    g_webServer.on("/heart-setup-exit", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, F("text/html"),
                  F("<!DOCTYPE html><html lang='de'><head><meta charset='utf-8'>"
                    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                    "</head><body><p>Wartungsseite beendet.</p>"
                    "</body></html>"));
        stopMqttMaintenanceHttp();
    });
}

static void maybeStopMqttMaintenanceHttpIfConfigured() {
    if (g_mqttMaintenanceHttpActive && mqtt_server[0] != '\0') {
        stopMqttMaintenanceHttp();
    }
}

static void maybeStartMqttMaintenanceHttp() {
    if (g_mqttMaintenanceHttpActive) {
        return;
    }
    if (mqtt_server[0] != '\0') {
        return;
    }
    using S = Mycila::ESPConnect::State;
    if (g_espConnect.getState() != S::NETWORK_CONNECTED) {
        return;
    }
    if (WiFi.status() != WL_CONNECTED || WiFi.localIP()[0] == 0) {
        return;
    }

    g_webServer.begin();
    g_mqttMaintenanceHttpActive = true;
    CONFIG_DBG_PRINT("MQTT-Wartungs-HTTP: http://");
    CONFIG_DBG_PRINT(WiFi.localIP());
    CONFIG_DBG_PRINTLN("/mqtt");
}

// ─── NVS: MQTT ────────────────────────────────────────────────────────────────

void loadMQTTConfig() {
    if (!preferences.begin("mqtt", true)) {
        CONFIG_DBG_PRINTLN("NVS mqtt: lesen fehlgeschlagen, nutze Defaults.");
        return;
    }
    preferences.getString("server", mqtt_server, sizeof(mqtt_server));
    const int p = preferences.getInt("port", 8883);
    mqtt_port   = (p > 0 && p <= 65535) ? static_cast<uint16_t>(p) : 8883;
    preferences.getString("user", mqtt_username, sizeof(mqtt_username));
    preferences.getString("pass", mqtt_password, sizeof(mqtt_password));
    if (preferences.getString("topic_pub", mqtt_topic_pub, sizeof(mqtt_topic_pub)) == 0
        || mqtt_topic_pub[0] == '\0') {
        safeStrCopy(mqtt_topic_pub, sizeof(mqtt_topic_pub), "heart/to_b");
    }
    if (preferences.getString("topic_sub", mqtt_topic_sub, sizeof(mqtt_topic_sub)) == 0
        || mqtt_topic_sub[0] == '\0') {
        safeStrCopy(mqtt_topic_sub, sizeof(mqtt_topic_sub), "heart/to_a");
    }
    preferences.end();
}

void saveMQTTConfig() {
    if (!preferences.begin("mqtt", false)) {
        CONFIG_DBG_PRINTLN("NVS mqtt: schreiben fehlgeschlagen.");
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

// ─── NVS: Herz-Zähler ─────────────────────────────────────────────────────────

void loadHeartCounter() {
    if (!preferences.begin("heart", true)) {
        CONFIG_DBG_PRINTLN("NVS heart: lesen fehlgeschlagen, Zaehler = 0.");
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
        CONFIG_DBG_PRINTLN("NVS heart: schreiben fehlgeschlagen.");
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
    registerMqttRoutes();

    const bool portalFromButton = consumePortalButtonRequest();

    Mycila::ESPConnect::Config cfg = {};
    cfg.hostname = kDeviceHostname;
    cfg.apMode   = false;
    loadWifiIntoConfig(cfg);

    if (portalFromButton) {
        cfg.wifiSSID.clear();
        cfg.wifiPassword.clear();
        CONFIG_DBG_PRINTLN("Wartungs-Captive-Portal (Taste): WLAN-Zugangsdaten zur Neuwahl.");
    }

    g_espConnect.listen([](Mycila::ESPConnect::State /*previous*/, Mycila::ESPConnect::State state) {
        if (state == Mycila::ESPConnect::State::PORTAL_COMPLETE) {
            const Mycila::ESPConnect::Config& c = g_espConnect.getConfig();
            if (!c.apMode) {
                saveWifiFromConfig(c);
            }
            flushHeartCounterIfDirty();
        }
    });

    g_espConnect.setAutoRestart(true);
    // Blocking true wartet nicht auf PORTAL_STARTED → Deadlock im Captive Portal.
    g_espConnect.setBlocking(false);
    g_espConnect.begin(kSetupApSsid, "", cfg);

    CONFIG_DBG_PRINTLN("ESPConnect gestartet (non-blocking), warte auf STA oder Neustart...");
    while (!wifiSetupGoalReached(g_espConnect)) {
        g_espConnect.loop();
        delay(10);
        // Nach erfolgreichem Portal folgt i.d.R. ESP.restart(); diese Schleife endet dann dort.
    }

    WiFi.setSleep(true);
    esp_wifi_set_ps(WIFI_PS_MAX_MODEM);
    (void)esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20);

    CONFIG_DBG_PRINT("WLAN bereit, STA-IP: ");
    CONFIG_DBG_PRINTLN(WiFi.localIP());
}

// ─── Factory Reset ────────────────────────────────────────────────────────────

void resetAllSettings() {
    CONFIG_DBG_PRINTLN("Factory Reset: alle Einstellungen loeschen...");
    stopMqttMaintenanceHttp();
    g_espConnect.clearConfiguration();
    g_espConnect.end();
    WiFi.disconnect(true, true);
    if (preferences.begin("wifi", false)) {
        preferences.clear();
        preferences.end();
    }
    if (preferences.begin("mqtt", false)) {
        preferences.clear();
        preferences.end();
    }
    if (preferences.begin(kPortalPrefsNs, false)) {
        preferences.clear();
        preferences.end();
    }
    flushHeartCounterIfDirty();
    delay(500);
    ESP.restart();
}

// ─── Loop ─────────────────────────────────────────────────────────────────────

void configLoop() {
    g_espConnect.loop();
    maybeStopMqttMaintenanceHttpIfConfigured();
    maybeStartMqttMaintenanceHttp();
}

bool configIsSetupPortalActive() {
    using S = Mycila::ESPConnect::State;
    const S st = g_espConnect.getState();
    if (st == S::PORTAL_STARTING || st == S::PORTAL_STARTED) {
        return true;
    }
    return g_mqttMaintenanceHttpActive;
}

void requestSetupPortalFromButton() {
    if (!preferences.begin(kPortalPrefsNs, false)) {
        CONFIG_DBG_PRINTLN("NVS cfg: Portal-Marker konnte nicht gesetzt werden.");
        return;
    }
    preferences.putBool(kPortalBtnKey, true);
    preferences.end();
    flushHeartCounterIfDirty();
    delay(200);
    ESP.restart();
}

Mycila::ESPConnect& configEspConnect() {
    return g_espConnect;
}
