#include "web_admin.h"

#include "constants.h"
#include "counter.h"
#include "display.h"
#include "mqtt.h"
#include "mqtt_config.h"
#include "ota.h"
#include "web_pages.h"
#include "wlan.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <esp_log.h>
#include <freertos/portmacro.h>

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

static MqttConfig        g_mqttPendingCfg;
static portMUX_TYPE      s_mqttCfgMux = portMUX_INITIALIZER_UNLOCKED;

static bool parseBodyParam(AsyncWebServerRequest* req, const char* name, char* out, size_t outLen) {
    if (req == nullptr || !req->hasParam(name, true)) {
        return false;
    }
    const AsyncWebParameter* p = req->getParam(name, true);
    if (p == nullptr) {
        return false;
    }
    strlcpy(out, p->value().c_str(), outLen);
    return true;
}

static void handleWifiConnectPost(AsyncWebServerRequest* req) {
    char ssid[33];
    char password[65];
    ssid[0]    = '\0';
    password[0] = '\0';
    if (!parseBodyParam(req, "ssid", ssid, sizeof(ssid)) || ssid[0] == '\0') {
        req->redirect(F("/wifi"));
        return;
    }
    (void)parseBodyParam(req, "password", password, sizeof(password));
    if (!configSaveWiFiCredentials(ssid, password)) {
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
    char url[256];
    url[0] = '\0';
    if (!parseBodyParam(req, "url", url, sizeof(url)) || std::strlen(url) < 10) {
        req->redirect(F("/update"));
        return;
    }
    if (!otaQueueFirmwareUrl(url)) {
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
        const AsyncWebParameter* p = req->getParam("mqtt_server", true);
        if (p != nullptr) {
            strlcpy(pending.server, p->value().c_str(), sizeof(pending.server));
        }
    }
    if (req->hasParam("mqtt_port", true)) {
        const AsyncWebParameter* p = req->getParam("mqtt_port", true);
        if (p != nullptr) {
            pending.port = normalizeMqttPort(atoi(p->value().c_str()));
        }
    }
    if (req->hasParam("mqtt_user", true)) {
        const AsyncWebParameter* p = req->getParam("mqtt_user", true);
        if (p != nullptr) {
            strlcpy(pending.username, p->value().c_str(), sizeof(pending.username));
        }
    }
    if (req->hasParam("mqtt_pass", true)) {
        const AsyncWebParameter* p = req->getParam("mqtt_pass", true);
        if (p != nullptr) {
            strlcpy(pending.password, p->value().c_str(), sizeof(pending.password));
        }
    }
    if (req->hasParam("mqtt_topic_pub", true)) {
        const AsyncWebParameter* p = req->getParam("mqtt_topic_pub", true);
        if (p != nullptr) {
            strlcpy(pending.topicPub, p->value().c_str(), sizeof(pending.topicPub));
        }
    }
    if (req->hasParam("mqtt_topic_sub", true)) {
        const AsyncWebParameter* p = req->getParam("mqtt_topic_sub", true);
        if (p != nullptr) {
            strlcpy(pending.topicSub, p->value().c_str(), sizeof(pending.topicSub));
        }
    }
    if (strcmp(pending.topicPub, pending.topicSub) == 0) {
        ESP_LOGW(TAG, "MQTT: Pub/Sub-Topic identisch, setze Defaults");
        strlcpy(pending.topicPub, kMqttDefaultTopicPub, sizeof(pending.topicPub));
        strlcpy(pending.topicSub, kMqttDefaultTopicSub, sizeof(pending.topicSub));
    }
    portENTER_CRITICAL(&s_mqttCfgMux);
    g_mqttPendingCfg = pending;
    portEXIT_CRITICAL(&s_mqttCfgMux);
    g_mqttApplyPending.store(true, std::memory_order_release);
    req->redirect(F("/mqtt?saved=1"));
}

static void handleSettingsPost(AsyncWebServerRequest* req) {
    bool weekly = false;
    if (req->hasParam("reset_period", true)) {
        const AsyncWebParameter* p = req->getParam("reset_period", true);
        if (p != nullptr) {
            weekly = (p->value() == "weekly");
        }
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
        MqttConfig localCfg;
        portENTER_CRITICAL(&s_mqttCfgMux);
        localCfg = g_mqttPendingCfg;
        portEXIT_CRITICAL(&s_mqttCfgMux);
        mqttCfg = localCfg;
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
