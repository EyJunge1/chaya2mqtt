#include "web_admin.h"

#include "config.h"
#include "mqtt.h"
#include "web_pages.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <HTTPUpdate.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
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

static bool     g_routesRegistered    = false;
static bool     g_rebootRequested     = false;
static bool     g_wifiConnectRequested = false;
static bool     g_otaRequested         = false;
static String   g_otaUrl;

static bool parseBodyParam(AsyncWebServerRequest* req, const char* name, String* outStr) {
    if (req == nullptr || !req->hasParam(name, true)) {
        return false;
    }
    *outStr = req->getParam(name, true)->value();
    return true;
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

static void handleUpdatePost(AsyncWebServerRequest* req) {
    String url;
    if (!parseBodyParam(req, "url", &url) || url.length() < 10) {
        req->redirect(F("/update"));
        return;
    }
    g_otaUrl       = url;
    g_otaRequested = true;
    streamSimpleDonePage(req, "Update", "Update wird gestartet …");
}

static void handleRebootPost(AsyncWebServerRequest* req) {
    g_rebootRequested = true;
    streamSimpleDonePage(req, "Neustart", "Neustart …");
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
