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

static const char* TAG __attribute__((unused)) = "WEB";

AsyncWebServer& webAdminWebServer() {
    static AsyncWebServer server(80);
    return server;
}

static bool g_mqttMaintenanceHttpActive = false;
static bool g_mqttRoutesRegistered      = false;

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
    appendHtmlEscaped(html, mqttCfg.server);
    char portBuf[8];
    snprintf(portBuf, sizeof(portBuf), "%u", static_cast<unsigned>(mqttCfg.port));
    html += F("'/>"
              "<label for='prt'>Port</label>"
              "<input id='prt' name='mqtt_port' type='number' min='1' max='65535' value='");
    html += portBuf;
    html += F("'/>"
              "<label for='usr'>Benutzername (optional)</label>"
              "<input id='usr' name='mqtt_user' maxlength='63' value='");
    appendHtmlEscaped(html, mqttCfg.username);
    html += F("'/>"
              "<label for='pw'>Passwort (optional)</label>"
              "<input id='pw' name='mqtt_pass' type='password' maxlength='63' "
              "autocomplete='current-password' value='");
    appendHtmlEscaped(html, mqttCfg.password);
    html += F("'/>"
              "<label for='tpub'>Sende-Topic</label>"
              "<input id='tpub' name='mqtt_topic_pub' maxlength='127' value='");
    appendHtmlEscaped(html, mqttCfg.topicPub);
    html += F("'/>"
              "<label for='tsub'>Empfangs-Topic</label>"
              "<input id='tsub' name='mqtt_topic_sub' maxlength='127' value='");
    appendHtmlEscaped(html, mqttCfg.topicSub);
    html += F("'/>"
              "<button type='submit'>Speichern</button></form>"
              "<nav><a class='btn' href='/heart-setup-exit'>Wartungsseite beenden</a></nav>"
              "</body></html>");
    return html;
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
    req->redirect(F("/mqtt?saved=1"));
}

void webAdminRegisterMqttRoutes() {
    if (g_mqttRoutesRegistered) {
        return;
    }
    g_mqttRoutesRegistered = true;

    AsyncWebServer& ws = webAdminWebServer();
    ws.on("/mqtt", HTTP_GET, [](AsyncWebServerRequest* req) {
        String banner;
        if (req->hasParam("saved")) {
            banner = F("<p class='ok'>&#10003; Gespeichert. MQTT wird neu verbunden.</p>");
        }
        req->send(200, F("text/html"), buildMqttPage(banner.c_str()));
    });
    ws.on("/mqtt", HTTP_POST, handleMqttPost);

    ws.on("/heart-setup-exit", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, F("text/html"),
                  F("<!DOCTYPE html><html lang='de'><head><meta charset='utf-8'>"
                    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
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
    g_mqttMaintenanceHttpActive = false;
    ESP_LOGI(TAG, "MQTT-Wartungs-HTTP gestoppt");
}

void webAdminMaybeStopMaintenanceIfBrokerConfigured() {
    if (g_mqttMaintenanceHttpActive && mqttCfg.server[0] != '\0') {
        webAdminStopMaintenanceHttp();
    }
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
