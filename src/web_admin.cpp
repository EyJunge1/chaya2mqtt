#include "web_admin.h"

#include "config.h"
#include "mqtt.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <HTTPUpdate.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiType.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <esp_log.h>

#if defined(CORE_DEBUG_LEVEL) && CORE_DEBUG_LEVEL > 0
static const char* TAG = "WEB";
#else
static constexpr const char* TAG __attribute__((unused)) = "";
#endif

AsyncWebServer& webAdminWebServer() {
    static AsyncWebServer server(80);
    return server;
}

static bool     g_routesRegistered        = false;
static bool     g_rebootRequested         = false;
static bool     g_wifiConnectRequested = false;
static bool     g_otaRequested           = false;
static String g_otaUrl;

static void appendHtmlEscaped(Print& out, const char* s);
static void appendJsonEscapedString(Print& out, const String& str);
static void printCommonCss(Print& out);
static bool parseBodyParam(AsyncWebServerRequest* req, const char* name, String* outStr);
static void streamPageHeader(Print& out, const char* title);

static bool parseBodyParam(AsyncWebServerRequest* req, const char* name, String* outStr) {
    if (req == nullptr || !req->hasParam(name, true)) {
        return false;
    }
    *outStr = req->getParam(name, true)->value();
    return true;
}

static void appendHtmlEscaped(Print& out, const char* s) {
    if (s == nullptr) {
        return;
    }
    for (; *s != '\0'; ++s) {
        switch (*s) {
            case '&': out.print(F("&amp;")); break;
            case '"': out.print(F("&quot;")); break;
            case '<': out.print(F("&lt;")); break;
            case '>': out.print(F("&gt;")); break;
            default: out.print(*s); break;
        }
    }
}

static void appendJsonEscapedString(Print& out, const String& str) {
    out.print('"');
    for (size_t i = 0; i < str.length(); ++i) {
        const unsigned char c = static_cast<unsigned char>(str[i]);
        switch (c) {
            case '"': out.print(F("\\\"")); break;
            case '\\': out.print(F("\\\\")); break;
            case '\b': out.print(F("\\b")); break;
            case '\f': out.print(F("\\f")); break;
            case '\n': out.print(F("\\n")); break;
            case '\r': out.print(F("\\r")); break;
            case '\t': out.print(F("\\t")); break;
            default:
                if (c < 0x20U) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out.print(buf);
                } else {
                    out.write(static_cast<char>(c));
                }
                break;
        }
    }
    out.print('"');
}

static void printCommonCss(Print& out) {
    out.print(
        F("<style>:root{"
          "--bg:#0a0a0a;--surface:#141414;--surface-hover:#1a1a1a;"
          "--border:#1a1a1a;--text:#dff9e3;--text-bright:#fff;"
          "--accent:#1dae6b;--accent-alt:#8597ff;"
          "--danger:#f44336;--success:#76d39e}"
          "*{box-sizing:border-box}"
          "html{color-scheme:dark}"
          "body{font-family:system-ui,sans-serif;margin:0;padding:16px;max-width:560px;"
          "background:var(--bg);color:var(--text)}"
          "h1{font-size:1.3rem;margin-bottom:4px;color:var(--text-bright)}"
          "label{display:block;margin:12px 0 4px;font-weight:600;color:var(--text-bright)}"
          "input{width:100%;padding:8px;border:1px solid var(--border);border-radius:4px;"
          "font-size:1rem;background:var(--surface);color:var(--text);"
          "transition:border-color .15s}"
          "input:focus{border-color:var(--accent);outline:none}"
          "button:not(.card){display:inline-block;margin-top:14px;padding:10px 20px;"
          "background:var(--accent);color:var(--bg);border:none;border-radius:4px;"
          "cursor:pointer;font-size:1rem;font-weight:600;transition:opacity .15s}"
          "button:not(.card):active{opacity:.8}"
          ".grid{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-top:16px}"
          ".full{grid-column:1/-1}"
          ".card{padding:24px 16px;background:var(--surface);border-radius:8px;text-align:center;"
          "text-decoration:none;color:var(--text);font-weight:600;font-size:1.1rem;"
          "border:1px solid var(--border);transition:background .15s,transform .1s}"
          ".card:hover{background:var(--surface-hover);transform:translateY(-2px)}"
          ".card:active{transform:translateY(0)}"
          ".card.danger{background:var(--surface);color:var(--danger);"
          "border-color:#2a0a0a;cursor:pointer;width:100%;"
          "font-family:inherit;font-size:1.1rem;font-weight:600;"
          "padding:24px 16px;border-radius:8px;transition:background .15s,transform .1s}"
          ".card.danger:hover{background:var(--surface-hover);transform:translateY(-2px)}"
          ".back{display:inline-block;margin-bottom:12px;color:var(--accent-alt);"
          "text-decoration:none;transition:opacity .15s}"
          ".back:hover{opacity:.7}"
          ".ok{color:var(--success);font-weight:600;margin:8px 0}"
          ".err{color:var(--danger);font-weight:600;margin:8px 0}"
          ".hint{color:var(--text);opacity:.85;font-size:.9rem;margin:8px 0;line-height:1.4}"
          "ul{padding-left:18px}li{margin:6px 0}"
          "</style>"));
}

static void streamPageHeader(Print& out, const char* title) {
    out.print(F("<!DOCTYPE html><html lang='de'><head><meta charset='utf-8'>"
                "<meta name='viewport' content='width=device-width,initial-scale=1'><title>"));
    out.print(title);
    out.print(F("</title>"));
    printCommonCss(out);
    out.print(F("</head><body>"));
}

static void streamSimpleDonePage(AsyncWebServerRequest* req, const char* title, const char* message) {
    AsyncResponseStream* resp = req->beginResponseStream("text/html");
    streamPageHeader(*resp, title);
    resp->print(F("<a class='back' href='/'>Dashboard</a><h1>"));
    resp->print(title);
    resp->print(F("</h1><p class='ok'>"));
    resp->print(message);
    resp->print(F("</p></body></html>"));
    req->send(resp);
}

static void streamDashboard(AsyncWebServerRequest* req) {
    AsyncResponseStream* resp = req->beginResponseStream("text/html");
    streamPageHeader(*resp, "Dashboard");
    resp->print(F("<h1>Chaya MQTT</h1><p class='hint'>"));
    if (configIsApMode()) {
        resp->print(F("Einrichtungs-AP: "));
        resp->print(WiFi.softAPIP().toString());
    } else if (WiFi.status() == WL_CONNECTED) {
        resp->print(F("Zugriff: "));
        resp->print(WiFi.localIP().toString());
        resp->print(F(" oder <strong>"));
        resp->print(WiFi.getHostname());
        resp->print(F(".local</strong>"));
    } else {
        resp->print(F("Kein Stations-WLAN."));
    }
    resp->print(F("</p><div class='grid'><a class='card' href='/wifi'>WLAN</a>"
                  "<a class='card' href='/mqtt'>MQTT</a>"
                  "<a class='card' href='/update'>Firmware-Update</a>"
                  "<form method='post' action='/reboot' class='full'>"
                  "<button type='submit' class='card danger'>Neustart</button></form>"
                  "</div></body></html>"));
    req->send(resp);
}

static void streamWifiPage(AsyncWebServerRequest* req) {
    WiFi.scanDelete();
#ifndef ESP8266
    WiFi.scanNetworks(true, false, false, 500, 0, nullptr, nullptr);
#else
    WiFi.scanNetworks(true);
#endif

    AsyncResponseStream* resp = req->beginResponseStream("text/html");
    streamPageHeader(*resp, "WLAN");
    resp->print(F("<a class='back' href='/'>Dashboard</a><h1>WLAN einrichten</h1>"
                  "<p class='hint'>"));
    if (WiFi.status() == WL_CONNECTED && WiFi.localIP()[0] != 0) {
        resp->print(F("Verbunden: <strong>"));
        appendHtmlEscaped(*resp, WiFi.SSID().c_str());
        resp->print(F("</strong>, IP "));
        resp->print(WiFi.localIP().toString());
        resp->print(F(", RSSI "));
        resp->print(static_cast<int>(WiFi.RSSI()));
        resp->print(F(" dBm"));
    } else {
        resp->print(F("Aktuell kein Stations-WLAN (AP- oder Einrichtungsmodus)."));
    }
    resp->print(
        F("</p>"
          "<form method='post' action='/wifi-connect' id='wf'>"
          "<label for='ssid'>SSID</label>"
          "<input name='ssid' id='ssid' required maxlength='32' autocomplete='off'/>"
          "<label for='pwd'>Passwort</label>"
          "<input name='password' id='pwd' type='password' maxlength='64' autocomplete='current-password'/>"
          "<button type='submit'>Verbinden (&amp; Neustart)</button></form>"
          "<h2>Scan</h2><p class='hint' id='st'>Scan läuft …</p><ul id='list'></ul>"
          "<script>"
          "(function(){"
          "var ss=document.getElementById('ssid'),lst=document.getElementById('list'),st=document.getElementById('st');"
          "function poll(){"
          "fetch('/wifi-scan').then(function(r){"
          "if(r.status===202){st.textContent='Scan läuft …';return Promise.resolve(null);}"
          "return r.json();"
          "}).then(function(rows){"
          "if(rows===null)return;"
          "lst.innerHTML='';"
          "if(!rows||!rows.length){st.textContent='Keine Netze.';return;}"
          "st.textContent='Netz anklicken, Passwort eintragen, Verbinden drücken.';"
          "for(var i=0;i<rows.length;i++){"
          "var li=document.createElement('li');"
          "var a=document.createElement('a');a.href='#';"
          "(function(nm){"
          "a.onclick=function(ev){ev.preventDefault();ss.value=nm;return false};"
          "})(rows[i].ssid);"
          "var o=rows[i].open?', offen':'';"
          "a.textContent=rows[i].ssid+' ('+rows[i].rssi+' dBm'+o+')';"
          "li.appendChild(a);lst.appendChild(li);"
          "} "
          "}).catch(function(){st.textContent='Scan-Fehler.'});"
          "} setInterval(poll,1500);poll();})();"
          "</script></body></html>"));
    req->send(resp);
}

static void handleWifiScanJson(AsyncWebServerRequest* req) {
    const int16_t n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) {
        req->send(202);
        return;
    }
    if (n == WIFI_SCAN_FAILED) {
        WiFi.scanDelete();
#ifndef ESP8266
        WiFi.scanNetworks(true, false, false, 500, 0, nullptr, nullptr);
#else
        WiFi.scanNetworks(true);
#endif
        req->send(202);
        return;
    }

    AsyncResponseStream* resp = req->beginResponseStream("application/json");
    resp->print('[');
    for (int i = 0; i < n; ++i) {
        if (i > 0) {
            resp->print(',');
        }
        resp->print(F("{\"ssid\":"));
        appendJsonEscapedString(*resp, WiFi.SSID(i));
        resp->print(F(",\"rssi\":"));
        resp->print(static_cast<int>(WiFi.RSSI(i)));
        const bool open = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
        resp->print(open ? F(",\"open\":true}") : F(",\"open\":false}"));
    }
    resp->print(']');
    WiFi.scanDelete();
    req->send(resp);
}

static void handleWifiConnectPost(AsyncWebServerRequest* req) {
    String ssid, password;
    if (!parseBodyParam(req, "ssid", &ssid) || ssid.length() == 0) {
        req->redirect(F("/wifi"));
        return;
    }
    (void)parseBodyParam(req, "password", &password);
    if (!configSaveWiFiCredentials(ssid.c_str(), password.c_str())) {
        req->redirect(F("/wifi"));
        return;
    }
    g_wifiConnectRequested = true;
    streamSimpleDonePage(req, "WLAN", "WLAN wurde gespeichert. Das Gerät startet neu …");
}

static void streamUpdatePage(AsyncWebServerRequest* req) {
    AsyncResponseStream* resp = req->beginResponseStream("text/html");
    streamPageHeader(*resp, "Firmware");
    resp->print(F("<a class='back' href='/'>Dashboard</a><h1>Firmware-Update</h1>"
                  "<p class='hint'>HTTPS-URL einer .bin von GitHub (Release-Assets) eingeben. "
                  "Fortschritt erscheint im Seriellen Monitor.</p>"
                  "<form method='post' action='/update'><label for='url'>URL</label>"
                  "<input id='url' name='url' type='url' required placeholder='https://…'/><button type='submit'>"
                  "Jetzt aktualisieren</button></form></body></html>"));
    req->send(resp);
}

static void handleUpdatePost(AsyncWebServerRequest* req) {
    String url;
    if (!parseBodyParam(req, "url", &url) || url.length() < 10) {
        req->redirect(F("/update"));
        return;
    }
    g_otaUrl     = url;
    g_otaRequested = true;
    streamSimpleDonePage(req, "Update", "Update wird gestartet …");
}

static void handleRebootPost(AsyncWebServerRequest* req) {
    g_rebootRequested = true;
    streamSimpleDonePage(req, "Neustart", "Neustart …");
}

static void streamMqttHtmlPage(AsyncWebServerRequest* req, bool showSavedBanner) {
    AsyncResponseStream* response = req->beginResponseStream("text/html");
    streamPageHeader(*response, "MQTT");
    response->print(F("<a class='back' href='/'>Dashboard</a><h1>MQTT-Einstellungen</h1>"));
    if (showSavedBanner) {
        response->print(F("<p class='ok'>&#10003; Gespeichert. MQTT wird neu verbunden.</p>"));
    }
    response->print(F("<form method='post' action='/mqtt'>"
                      "<label for='srv'>Broker (Hostname oder IP)</label>"
                      "<input id='srv' name='mqtt_server' maxlength='127' value='"));
    appendHtmlEscaped(*response, mqttCfg.server);
    char portBuf[8];
    snprintf(portBuf, sizeof(portBuf), "%u", static_cast<unsigned>(mqttCfg.port));
    response->print(F("'/>"
                      "<label for='prt'>Port</label>"
                      "<input id='prt' name='mqtt_port' type='number' min='1' max='65535' value='"));
    response->print(portBuf);
    response->print(F("'/>"
                      "<label for='usr'>Benutzername (optional)</label>"
                      "<input id='usr' name='mqtt_user' maxlength='63' value='"));
    appendHtmlEscaped(*response, mqttCfg.username);
    response->print(F("'/>"
                      "<label for='pw'>Passwort (optional)</label>"
                      "<input id='pw' name='mqtt_pass' type='password' maxlength='63' "
                      "autocomplete='current-password' value='"));
    appendHtmlEscaped(*response, mqttCfg.password);
    response->print(F("'/>"
                      "<label for='tpub'>Sende-Topic</label>"
                      "<input id='tpub' name='mqtt_topic_pub' maxlength='127' value='"));
    appendHtmlEscaped(*response, mqttCfg.topicPub);
    response->print(F("'/>"
                      "<label for='tsub'>Empfangs-Topic</label>"
                      "<input id='tsub' name='mqtt_topic_sub' maxlength='127' value='"));
    appendHtmlEscaped(*response, mqttCfg.topicSub);
    response->print(F("'/>"
                      "<button type='submit'>Speichern</button></form>"
                      "</body></html>"));
    req->send(response);
}

static void handleMqttPost(AsyncWebServerRequest* req) {
    if (req->hasParam("mqtt_server", true)) {
        strlcpy(mqttCfg.server, req->getParam("mqtt_server", true)->value().c_str(),
                sizeof(mqttCfg.server));
    }
    if (req->hasParam("mqtt_port", true)) {
        const int p = atoi(req->getParam("mqtt_port", true)->value().c_str());
        mqttCfg.port = (p > 0 && p <= 65535) ? static_cast<uint16_t>(p) : 8883;
    }
    if (req->hasParam("mqtt_user", true)) {
        strlcpy(mqttCfg.username, req->getParam("mqtt_user", true)->value().c_str(),
                sizeof(mqttCfg.username));
    }
    if (req->hasParam("mqtt_pass", true)) {
        strlcpy(mqttCfg.password, req->getParam("mqtt_pass", true)->value().c_str(),
                sizeof(mqttCfg.password));
    }
    if (req->hasParam("mqtt_topic_pub", true)) {
        strlcpy(mqttCfg.topicPub, req->getParam("mqtt_topic_pub", true)->value().c_str(),
                sizeof(mqttCfg.topicPub));
    }
    if (req->hasParam("mqtt_topic_sub", true)) {
        strlcpy(mqttCfg.topicSub, req->getParam("mqtt_topic_sub", true)->value().c_str(),
                sizeof(mqttCfg.topicSub));
    }
    if (strcmp(mqttCfg.topicPub, mqttCfg.topicSub) == 0) {
        ESP_LOGW(TAG, "MQTT: Pub/Sub-Topic identisch, setze Defaults");
        strlcpy(mqttCfg.topicPub, "heart/to_b", sizeof(mqttCfg.topicPub));
        strlcpy(mqttCfg.topicSub, "heart/to_a", sizeof(mqttCfg.topicSub));
    }
    saveMQTTConfig();
    mqttDisconnect();
    mqttSetup();
    req->redirect(F("/mqtt?saved=1"));
}

void webAdminRegisterRoutes() {
    if (g_routesRegistered) {
        return;
    }
    g_routesRegistered = true;

    AsyncWebServer& ws = webAdminWebServer();
    ws.on("/", HTTP_GET, [](AsyncWebServerRequest* rq) {
        streamDashboard(rq);
    });
    ws.on("/wifi", HTTP_GET, [](AsyncWebServerRequest* rq) {
        streamWifiPage(rq);
    });
    ws.on("/wifi-scan", HTTP_GET, [](AsyncWebServerRequest* rq) {
        handleWifiScanJson(rq);
    });
    ws.on("/wifi-connect", HTTP_POST, [](AsyncWebServerRequest* rq) {
        handleWifiConnectPost(rq);
    });
    ws.on("/update", HTTP_GET, [](AsyncWebServerRequest* rq) {
        streamUpdatePage(rq);
    });
    ws.on("/update", HTTP_POST, [](AsyncWebServerRequest* rq) {
        handleUpdatePost(rq);
    });
    ws.on("/reboot", HTTP_POST, [](AsyncWebServerRequest* rq) {
        handleRebootPost(rq);
    });

    ws.on("/mqtt", HTTP_GET, [](AsyncWebServerRequest* rq) {
        streamMqttHtmlPage(rq, rq->hasParam("saved"));
    });
    ws.on("/mqtt", HTTP_POST, handleMqttPost);

    ws.onNotFound([](AsyncWebServerRequest* rq) {
        rq->redirect(F("/"));
    });
}

void webAdminLoop() {
    if (g_rebootRequested) {
        delay(200);
        ESP.restart();
    }
    if (g_wifiConnectRequested) {
        delay(200);
        ESP.restart();
    }
    if (g_otaRequested) {
        g_otaRequested = false;
        WiFiClientSecure client;
        client.setInsecure();
        HTTPUpdate httpUpdate;
        httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
        const t_httpUpdate_return rc = httpUpdate.update(client, g_otaUrl);
        if (rc == HTTP_UPDATE_OK) {
            ESP_LOGI(TAG, "OTA ok, Neustart");
            delay(200);
            ESP.restart();
        } else if (rc == HTTP_UPDATE_NO_UPDATES) {
            ESP_LOGW(TAG, "OTA: keine Updates");
        } else {
            ESP_LOGE(TAG, "OTA Fehler=%d (%s)",
                     static_cast<int>(httpUpdate.getLastError()), httpUpdate.getLastErrorString().c_str());
        }
    }
}
