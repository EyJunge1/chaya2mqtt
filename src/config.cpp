#include "config.h"

#include "mqtt.h"

#include <Arduino.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <WiFi.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <esp_wifi.h>

#if defined(CORE_DEBUG_LEVEL) && CORE_DEBUG_LEVEL > 0
#define CONFIG_DBG_PRINT(x)   Serial.print(x)
#define CONFIG_DBG_PRINTLN(x) Serial.println(x)
#else
#define CONFIG_DBG_PRINT(x)   ((void)0)
#define CONFIG_DBG_PRINTLN(x) ((void)0)
#endif

// ─── Globale MQTT-Variablen (Deklarationen in config.h) ─────────────────────

char     mqtt_server[128]    = "";
uint16_t mqtt_port           = 8883;
char     mqtt_username[64]   = "";
char     mqtt_password[64]   = "";
char     mqtt_topic_pub[128] = "heart/to_b";
char     mqtt_topic_sub[128] = "heart/to_a";

// ─── Herz-Zähler ────────────────────────────────────────────────────────────

int heartCounter = 0;

static int           lastCommittedHeartCounter            = 0;
static unsigned long lastHeartCounterSaveMs               = 0;
static constexpr unsigned long kHeartCounterSaveMinIntervalMs = 30000;

// ─── Interner Zustand ────────────────────────────────────────────────────────

static Preferences preferences;

static constexpr char    kSetupApSsid[] = "HeartESP32-Setup";
static constexpr uint8_t kDnsPort       = 53;
static const IPAddress   kApIp(192, 168, 4, 1);
static const IPAddress   kApGw(192, 168, 4, 1);
static const IPAddress   kApSn(255, 255, 255, 0);

static AsyncWebServer g_server(80);
static DNSServer      g_dns;

static bool g_portalActive      = false;
static bool g_routesReady       = false;
static bool g_stopPortalPending = false;  // gesetzt im /exit-Handler, ausgeführt in configLoop()
static bool g_wifiSaved         = false;  // gesetzt im /wifi-POST-Handler, setupWiFi() wartet darauf

static char g_wifiSsid[64] = "";
static char g_wifiPass[64] = "";

// ─── Hilfsfunktionen ─────────────────────────────────────────────────────────

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
            case '&':  out += F("&amp;");  break;
            case '"':  out += F("&quot;"); break;
            case '<':  out += F("&lt;");   break;
            case '>':  out += F("&gt;");   break;
            default:   out += *s;          break;
        }
    }
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

// ─── Seitenbausteine ─────────────────────────────────────────────────────────

static String buildSetupHome() {
    String html;
    html.reserve(1024);
    html += F("<!DOCTYPE html><html lang='de'><head><meta charset='utf-8'>"
              "<meta name='viewport' content='width=device-width,initial-scale=1'>"
              "<title>HeartESP32 Einrichtung</title>");
    html += commonCss();
    html += F("</head><body><h1>&#10084; HeartESP32 Einrichtung</h1><ul>"
              "<li><a href='/wifi'>WLAN konfigurieren</a></li>"
              "<li><a href='/mqtt'>MQTT konfigurieren</a></li>"
              "</ul><p>WLAN: <strong>");
    if (WiFi.status() == WL_CONNECTED) { // NOLINT(readability-static-accessed-through-instance)
        appendHtmlEscaped(html, WiFi.SSID().c_str());
        html += F("</strong> &mdash; IP: <strong>");
        html += WiFi.localIP().toString();
    } else {
        html += F("nicht verbunden");
    }
    html += F("</strong></p>"
              "<nav><a class='btn' href='/heart-setup-exit'>Wartungsmodus beenden</a></nav>"
              "</body></html>");
    return html;
}

static String buildWifiPage(const char* banner) {
    String html;
    html.reserve(1200);
    html += F("<!DOCTYPE html><html lang='de'><head><meta charset='utf-8'>"
              "<meta name='viewport' content='width=device-width,initial-scale=1'>"
              "<title>WLAN</title>");
    html += commonCss();
    html += F("</head><body><h1>WLAN-Einstellungen</h1>");
    if (banner != nullptr && banner[0] != '\0') {
        html += banner;
    }
    html += F("<form method='post' action='/wifi'>"
              "<label for='ssid'>Netzwerkname (SSID)</label>"
              "<input id='ssid' name='ssid' maxlength='63' autocomplete='off' value='");
    appendHtmlEscaped(html, g_wifiSsid);
    html += F("'/>"
              "<label for='pass'>Passwort</label>"
              "<input id='pass' name='pass' type='password' maxlength='63' "
              "autocomplete='current-password'/>"
              "<button type='submit'>Speichern &amp; Neustart</button></form>"
              "<nav><a href='/'>&#8592; Zur&uuml;ck</a></nav></body></html>");
    return html;
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
              "<nav><a href='/'>&#8592; Zur&uuml;ck</a></nav></body></html>");
    return html;
}

// ─── Benannte Request-Handler (reduzieren Komplexität von registerRoutes) ────

static void handleWifiPost(AsyncWebServerRequest* req) {
    char ssid[64] = "";
    char pass[64] = "";
    if (req->hasParam("ssid", true)) {
        safeStrCopy(ssid, sizeof(ssid), req->getParam("ssid", true)->value().c_str());
    }
    if (req->hasParam("pass", true)) {
        safeStrCopy(pass, sizeof(pass), req->getParam("pass", true)->value().c_str());
    }
    if (ssid[0] == '\0') {
        req->redirect(F("/wifi?err=1"));
        return;
    }
    if (preferences.begin("wifi", false)) {
        preferences.putString("ssid", ssid);
        preferences.putString("pass", pass);
        preferences.end();
    }
    safeStrCopy(g_wifiSsid, sizeof(g_wifiSsid), ssid);
    safeStrCopy(g_wifiPass, sizeof(g_wifiPass), pass);
    g_wifiSaved = true;
    req->send(200, F("text/html"),
              F("<!DOCTYPE html><html lang='de'><head><meta charset='utf-8'>"
                "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                "</head><body><h2>WLAN gespeichert.</h2>"
                "<p>Das Ger&auml;t verbindet sich neu &ndash; diese Seite kann geschlossen werden.</p>"
                "</body></html>"));
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

// ─── Web-Routen (einmalig registrieren) ─────────────────────────────────────

static void stopPortal();  // Vorwärtsdeklaration

static void registerRoutes() {
    if (g_routesReady) {
        return;
    }
    g_routesReady = true;

    // Captive Portal: alle unbekannten URLs → Startseite
    g_server.onNotFound([](AsyncWebServerRequest* req) {
        req->redirect(F("http://192.168.4.1/"));
    });

    g_server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, F("text/html"), buildSetupHome());
    });
    g_server.on("/heart-setup", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, F("text/html"), buildSetupHome());
    });

    g_server.on("/wifi", HTTP_GET, [](AsyncWebServerRequest* req) {
        String banner;
        if (req->hasParam("err")) {
            banner = F("<p class='err'>Kein Netzwerkname angegeben &ndash; bitte erneut versuchen.</p>");
        }
        req->send(200, F("text/html"), buildWifiPage(banner.c_str()));
    });
    g_server.on("/wifi", HTTP_POST, handleWifiPost);

    g_server.on("/mqtt", HTTP_GET, [](AsyncWebServerRequest* req) {
        String banner;
        if (req->hasParam("saved")) {
            banner = F("<p class='ok'>&#10003; Gespeichert. MQTT wird neu verbunden.</p>");
        }
        req->send(200, F("text/html"), buildMqttPage(banner.c_str()));
    });
    g_server.on("/mqtt", HTTP_POST, handleMqttPost);

    // Wartungsmodus beenden
    g_server.on("/heart-setup-exit", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, F("text/html"),
                  F("<!DOCTYPE html><html lang='de'><head><meta charset='utf-8'>"
                    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                    "</head><body><p>Wartungsmodus beendet. Dieses Fenster kann geschlossen werden.</p>"
                    "</body></html>"));
        g_stopPortalPending = true;
    });
}

// ─── Portal-Lebenszyklus ─────────────────────────────────────────────────────

static bool startPortal(bool apOnly) {
    if (g_portalActive) {
        CONFIG_DBG_PRINTLN("Setup-Portal bereits aktiv.");
        return true;
    }
    registerRoutes();

    WiFi.mode(apOnly ? WIFI_AP : WIFI_AP_STA); // NOLINT(readability-static-accessed-through-instance)
    WiFi.softAPConfig(kApIp, kApGw, kApSn);
    if (!WiFi.softAP(kSetupApSsid, "")) {
        CONFIG_DBG_PRINTLN("softAP Start fehlgeschlagen.");
        if (!apOnly) {
            WiFi.mode(WIFI_STA); // NOLINT(readability-static-accessed-through-instance)
        }
        return false;
    }

    g_dns.setErrorReplyCode(DNSReplyCode::NoError);
    g_dns.stop();
    if (!g_dns.start(kDnsPort, "*", WiFi.softAPIP())) {
        CONFIG_DBG_PRINTLN("DNS Start fehlgeschlagen.");
        WiFi.softAPdisconnect(true);
        if (!apOnly) {
            WiFi.mode(WIFI_STA); // NOLINT(readability-static-accessed-through-instance)
        }
        return false;
    }

    g_server.begin();
    g_portalActive = true;
    CONFIG_DBG_PRINT("Setup-Portal aktiv: http://");
    CONFIG_DBG_PRINTLN(WiFi.softAPIP().toString());
    return true;
}

static void stopPortal() {
    if (!g_portalActive) {
        return;
    }
    g_dns.stop();
    g_server.end();
    WiFi.mode(WIFI_STA); // NOLINT(readability-static-accessed-through-instance)
    g_portalActive = false;
    CONFIG_DBG_PRINTLN("Setup-Portal gestoppt.");
}

// ─── NVS: MQTT ───────────────────────────────────────────────────────────────

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

// ─── NVS: Herz-Zähler ────────────────────────────────────────────────────────

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

// ─── WiFi-Setup ─────────────────────────────────────────────────────────────

void setupWiFi() {
    registerRoutes();

    // Gespeicherte WLAN-Zugangsdaten laden
    if (preferences.begin("wifi", true)) {
        preferences.getString("ssid", g_wifiSsid, sizeof(g_wifiSsid));
        preferences.getString("pass", g_wifiPass, sizeof(g_wifiPass));
        preferences.end();
    }

    if (g_wifiSsid[0] != '\0') {
        CONFIG_DBG_PRINT("Verbinde mit WLAN: ");
        CONFIG_DBG_PRINTLN(g_wifiSsid);
        WiFi.mode(WIFI_STA); // NOLINT(readability-static-accessed-through-instance)
        WiFi.begin(g_wifiSsid, g_wifiPass);
        const unsigned long t0 = millis();
        while (WiFi.status() != WL_CONNECTED // NOLINT(readability-static-accessed-through-instance)
               && millis() - t0 < 15000) {
            delay(100);
        }
    }

    if (WiFi.status() != WL_CONNECTED) { // NOLINT(readability-static-accessed-through-instance)
        CONFIG_DBG_PRINTLN("WLAN nicht verbunden -> Captive Portal starten.");
        startPortal(true);  // nur AP, STA entfaellt
        // Blockiert in setup(), bis der Nutzer im Portal WLAN-Daten eingibt.
        // ESPAsyncWebServer laeuft im Hintergrund (eigener Task), DNS muss
        // manuell getriggert werden.
        while (!g_wifiSaved) {
            g_dns.processNextRequest();
            delay(10);
        }
        flushHeartCounterIfDirty();
        delay(500);
        ESP.restart();
        return;
    }

    CONFIG_DBG_PRINT("WLAN verbunden, IP: ");
    CONFIG_DBG_PRINTLN(WiFi.localIP());

    WiFi.setSleep(true);
    esp_wifi_set_ps(WIFI_PS_MAX_MODEM);
    (void)esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20);

    if (mqtt_server[0] == '\0') {
        CONFIG_DBG_PRINTLN("Kein MQTT-Broker -> parallelen Wartungs-AP starten.");
        startPortal(false);  // AP+STA parallel
    }
}

// ─── Factory Reset ───────────────────────────────────────────────────────────

void resetAllSettings() {
    CONFIG_DBG_PRINTLN("Factory Reset: alle Einstellungen loeschen...");
    stopPortal();
    WiFi.disconnect(true, true);  // Loescht auch intern gespeicherte WLAN-Credentials
    if (preferences.begin("wifi", false)) {
        preferences.clear();
        preferences.end();
    }
    if (preferences.begin("mqtt", false)) {
        preferences.clear();
        preferences.end();
    }
    flushHeartCounterIfDirty();
    delay(500);
    ESP.restart();
}

// ─── Loop (wird von main.cpp aufgerufen) ─────────────────────────────────────

void configLoop() {
    if (!g_portalActive) {
        return;
    }
    g_dns.processNextRequest();
    if (g_stopPortalPending) {
        g_stopPortalPending = false;
        stopPortal();
    }
}

bool configIsSetupPortalActive() {
    return g_portalActive;
}

void requestSetupPortalFromButton() {
    // NOLINT(readability-static-accessed-through-instance) für WiFi.status()
    startPortal(WiFi.status() != WL_CONNECTED); // NOLINT(readability-static-accessed-through-instance)
}
