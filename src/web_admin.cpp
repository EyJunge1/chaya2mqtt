#include "web_admin.h"

#include "config.h"
#include "mqtt.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <MycilaESPConnect.h>
#include <WiFi.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <esp_log.h>

#if defined(CORE_DEBUG_LEVEL) && CORE_DEBUG_LEVEL > 0
static const char* TAG = "WEB";
#else
static constexpr const char* TAG __attribute__((unused)) = "";
#endif

/** Nach MQTT-POST kurz warten, damit Redirect/Response fertig gesendet wird bevor der Server end() bekommt. */
static constexpr unsigned long kMaintenanceHttpStopDeferMs = 2000;

AsyncWebServer& webAdminWebServer() {
    static AsyncWebServer server(80);
    return server;
}

static bool g_mqttMaintenanceHttpActive = false;
static bool g_mqttRoutesRegistered      = false;

static bool                      g_maintenanceStopDeferActive = false;
static unsigned long             g_maintenanceStopEarliestMs  = 0;

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

static void printCommonCss(Print& out) {
    out.print(F("<style>*{box-sizing:border-box}"
                "html{color-scheme:dark}"
                "body{font-family:system-ui,sans-serif;margin:0;padding:16px;max-width:560px;"
                "background:#0a0a0a;color:#dff9e3}"
                "h1{font-size:1.3rem;margin-bottom:4px;color:#fff}"
                "label{display:block;margin:12px 0 4px;font-weight:600;color:#fff}"
                "input{width:100%;padding:8px;border:1px solid #1a1a1a;border-radius:4px;font-size:1rem;"
                "background:#141414;color:#dff9e3}"
                "button{display:inline-block;margin-top:14px;padding:10px 20px;"
                "background:#1dae6b;color:#0a0a0a;border:none;border-radius:4px;"
                "cursor:pointer;font-size:1rem;font-weight:600}"
                "a.btn{display:inline-block;margin-top:14px;padding:10px 20px;"
                "background:#8597ff;color:#fff;border-radius:4px;"
                "text-decoration:none;cursor:pointer;font-size:1rem}"
                ".ok{color:#76d39e;font-weight:600;margin:8px 0}"
                ".err{color:#f44336;font-weight:600;margin:8px 0}"
                "ul{padding-left:18px}li{margin:6px 0}"
                "nav{margin-top:20px}nav a{margin-right:12px}</style>"));
}

static void streamMqttHtmlPage(AsyncWebServerRequest* req, bool showSavedBanner) {
    AsyncResponseStream* response = req->beginResponseStream("text/html");
    response->print(F("<!DOCTYPE html><html lang='de'><head><meta charset='utf-8'>"
                      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                      "<title>MQTT</title>"));
    printCommonCss(*response);
    response->print(F("</head><body><h1>MQTT-Einstellungen</h1>"));
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
                      "<nav><a class='btn' href='/heart-setup-exit'>Wartungsseite beenden</a></nav>"
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
        mqttCfg.port   = (p > 0 && p <= 65535) ? static_cast<uint16_t>(p) : 8883;
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

    if (g_mqttMaintenanceHttpActive) {
        g_maintenanceStopDeferActive = true;
        g_maintenanceStopEarliestMs  = millis() + kMaintenanceHttpStopDeferMs;
    }

    req->redirect(F("/mqtt?saved=1"));
}

void webAdminRegisterMqttRoutes() {
    if (g_mqttRoutesRegistered) {
        return;
    }
    g_mqttRoutesRegistered = true;

    AsyncWebServer& ws = webAdminWebServer();
    ws.on("/mqtt", HTTP_GET, [](AsyncWebServerRequest* req) {
        streamMqttHtmlPage(req, req->hasParam("saved"));
    });
    ws.on("/mqtt", HTTP_POST, handleMqttPost);

    ws.on("/heart-setup-exit", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, F("text/html"),
                  F("<!DOCTYPE html><html lang='de'><head><meta charset='utf-8'>"
                    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                    "<style>html{color-scheme:dark}body{background:#0a0a0a;color:#dff9e3;"
                    "font-family:system-ui,sans-serif;margin:16px}</style>"
                    "</head><body><p>Wartungsseite beendet.</p>"
                    "</body></html>"));
        webAdminStopMaintenanceHttp();
    });
}

void webAdminStopMaintenanceHttp() {
    if (!g_mqttMaintenanceHttpActive) {
        return;
    }
    webAdminWebServer().end();
    g_mqttMaintenanceHttpActive   = false;
    g_maintenanceStopDeferActive  = false;
    g_maintenanceStopEarliestMs   = 0;
    ESP_LOGI(TAG, "MQTT-Wartungs-HTTP gestoppt");
}

void webAdminMaybeStopMaintenanceIfBrokerConfigured() {
    if (!g_mqttMaintenanceHttpActive || mqttCfg.server[0] == '\0') {
        return;
    }
    if (g_maintenanceStopDeferActive) {
        const unsigned long now = millis();
        if ((long)(now - g_maintenanceStopEarliestMs) < 0) {
            return;
        }
        g_maintenanceStopDeferActive = false;
    }
    webAdminStopMaintenanceHttp();
}

void webAdminMaybeStartMaintenance(Mycila::ESPConnect& espConnect) {
    if (g_mqttMaintenanceHttpActive) {
        return;
    }
    if (mqttCfg.server[0] != '\0') {
        return;
    }
    using S = Mycila::ESPConnect::State;
    if (espConnect.getState() != S::NETWORK_CONNECTED) {
        return;
    }
    if (WiFi.status() != WL_CONNECTED || WiFi.localIP()[0] == 0) {
        return;
    }

    webAdminWebServer().begin();
    g_mqttMaintenanceHttpActive = true;
    ESP_LOGI(TAG, "MQTT-Wartungs-HTTP: http://%s/mqtt", WiFi.localIP().toString().c_str());
}

bool webAdminIsMaintenanceHttpActive() {
    return g_mqttMaintenanceHttpActive;
}
