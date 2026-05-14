#include "web_admin.h"

#include "config.h"
#include "counter.h"
#include "display.h"
#include "mqtt.h"
#include "ota.h"
#include "web_pages.h"
#include "wlan.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <atomic>
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

static bool              g_routesRegistered = false;
static std::atomic<bool> g_rebootRequested{false};
static std::atomic<bool> g_wifiConnectRequested{false};
static std::atomic<bool> g_mqttApplyPending{false};

static MqttConfig g_mqttPendingCfg;

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
    g_wifiConnectRequested.store(true, std::memory_order_release);
    streamSimpleDonePage(req, "Wi-Fi",
        "Wi-Fi gespeichert. Ger&auml;t startet neu.<br>"
        "MQTT und weitere Einstellungen unter "
        "<strong>http://chaya2mqtt.local</strong> konfigurieren (gleiches WLAN).");
}

static void handleUpdatePost(AsyncWebServerRequest* req) {
    String url;
    if (!parseBodyParam(req, "url", &url) || url.length() < 10) {
        req->redirect(F("/update"));
        return;
    }
    if (!otaQueueFirmwareUrl(url.c_str())) {
        req->redirect(F("/update"));
        return;
    }
    streamSimpleDonePage(req, "Update", "Update starting…");
}

static void handleUpdateCheckPost(AsyncWebServerRequest* req) {
    otaQueueGithubCheck();
    streamSimpleDonePage(req, "Update", "Checking GitHub; an update may follow…");
}

static void handleRebootPost(AsyncWebServerRequest* req) {
    g_rebootRequested.store(true, std::memory_order_release);
    streamSimpleDonePage(req, "Reboot", "Rebooting…");
}

static void handleMqttPost(AsyncWebServerRequest* req) {
    MqttConfig pending = mqttCfg;

    if (req->hasParam("mqtt_server", true)) {
        strlcpy(pending.server, req->getParam("mqtt_server", true)->value().c_str(),
                sizeof(pending.server));
    }
    if (req->hasParam("mqtt_port", true)) {
        const int p = atoi(req->getParam("mqtt_port", true)->value().c_str());
        pending.port = (p > 0 && p <= 65535) ? static_cast<uint16_t>(p) : 8883;
    }
    if (req->hasParam("mqtt_user", true)) {
        strlcpy(pending.username, req->getParam("mqtt_user", true)->value().c_str(),
                sizeof(pending.username));
    }
    if (req->hasParam("mqtt_pass", true)) {
        strlcpy(pending.password, req->getParam("mqtt_pass", true)->value().c_str(),
                sizeof(pending.password));
    }
    if (req->hasParam("mqtt_topic_pub", true)) {
        strlcpy(pending.topicPub, req->getParam("mqtt_topic_pub", true)->value().c_str(),
                sizeof(pending.topicPub));
    }
    if (req->hasParam("mqtt_topic_sub", true)) {
        strlcpy(pending.topicSub, req->getParam("mqtt_topic_sub", true)->value().c_str(),
                sizeof(pending.topicSub));
    }
    if (strcmp(pending.topicPub, pending.topicSub) == 0) {
        ESP_LOGW(TAG, "MQTT: Pub/Sub-Topic identisch, setze Defaults");
        strlcpy(pending.topicPub, "chaya/to_b", sizeof(pending.topicPub));
        strlcpy(pending.topicSub, "chaya/to_a", sizeof(pending.topicSub));
    }
    g_mqttPendingCfg = pending;
    g_mqttApplyPending.store(true, std::memory_order_release);
    req->redirect(F("/mqtt?saved=1"));
}

static void handleSettingsPost(AsyncWebServerRequest* req) {
    bool weekly = false;
    if (req->hasParam("reset_period", true)) {
        const String v = req->getParam("reset_period", true)->value();
        weekly = (v == "weekly");
    }
    configSetResetPeriodWeekly(weekly);
    req->redirect(F("/settings?saved=1"));
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
        if (configIsApMode()) {
            rq->redirect(F("/"));
            return;
        }
        streamUpdatePage(rq);
    });
    ws.on("/update", HTTP_POST, [](AsyncWebServerRequest* rq) {
        if (configIsApMode()) {
            rq->redirect(F("/"));
            return;
        }
        handleUpdatePost(rq);
    });
    ws.on("/update-check", HTTP_POST, [](AsyncWebServerRequest* rq) {
        if (configIsApMode()) {
            rq->redirect(F("/"));
            return;
        }
        handleUpdateCheckPost(rq);
    });
    ws.on("/reboot", HTTP_POST, [](AsyncWebServerRequest* rq) {
        if (configIsApMode()) {
            rq->redirect(F("/"));
            return;
        }
        handleRebootPost(rq);
    });

    ws.on("/mqtt", HTTP_GET, [](AsyncWebServerRequest* rq) {
        if (configIsApMode()) {
            rq->redirect(F("/"));
            return;
        }
        streamMqttHtmlPage(rq, rq->hasParam("saved"));
    });
    ws.on("/mqtt", HTTP_POST, [](AsyncWebServerRequest* rq) {
        if (configIsApMode()) {
            rq->redirect(F("/"));
            return;
        }
        handleMqttPost(rq);
    });

    ws.on("/settings", HTTP_GET, [](AsyncWebServerRequest* rq) {
        if (configIsApMode()) {
            rq->redirect(F("/"));
            return;
        }
        streamSettingsPage(rq, rq->hasParam("saved"));
    });
    ws.on("/settings", HTTP_POST, [](AsyncWebServerRequest* rq) {
        if (configIsApMode()) {
            rq->redirect(F("/"));
            return;
        }
        handleSettingsPost(rq);
    });

    ws.onNotFound([](AsyncWebServerRequest* rq) {
        rq->redirect(F("/"));
    });
}

void webAdminLoop() {
    if (g_mqttApplyPending.exchange(false, std::memory_order_acq_rel)) {
        mqttCfg = g_mqttPendingCfg;
        saveMQTTConfig();
        mqttDisconnect();
        mqttSetup();
        if (mqttCfg.server[0] != '\0') {
            requestHeartRedraw();
        }
    }

    if (g_rebootRequested.exchange(false, std::memory_order_acq_rel)
        || g_wifiConnectRequested.exchange(false, std::memory_order_acq_rel)) {
        flushHeartCounterIfDirty();
        flushHeartSentCounterIfDirty();
        delay(200);
        releaseGpioHoldBeforeRestart();
        ESP.restart();
    }

    otaLoop();
}
