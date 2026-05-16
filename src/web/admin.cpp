#include "admin.h"

#include "constants.h"
#include "counter.h"
#include "display.h"
#include "mqtt.h"
#include "mqtt_config.h"
#include "ota.h"
#include "auth.h"
#include "pages.h"
#include "wlan.h"

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <atomic>
#include <cerrno>
#include <cstdint>
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

static bool              g_routesRegistered = false;
static std::atomic<bool> g_rebootRequested{false};
static std::atomic<bool> g_wifiConnectRequested{false};
static std::atomic<bool> g_mqttApplyPending{false};
static std::atomic<bool> g_settingsApplyPending{false};

static uint8_t           s_pendingResetDays     = 7;
static bool              s_pendingWebAuthEnabled = false;
static portMUX_TYPE      s_settingsPendingMux   = portMUX_INITIALIZER_UNLOCKED;

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

/** Minimal JSON string escape for /wifi-connect-status SSID field. */
static void appendJsonEscapedCStrWifiStatus(Print& out, const char* str) {
    out.print('"');
    if (str == nullptr) {
        out.print('"');
        return;
    }
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(str); *p != '\0'; ++p) {
        const unsigned char c = *p;
        switch (c) {
            case '"':
                out.print(F("\\\""));
                break;
            case '\\':
                out.print(F("\\\\"));
                break;
            case '\b':
                out.print(F("\\b"));
                break;
            case '\f':
                out.print(F("\\f"));
                break;
            case '\n':
                out.print(F("\\n"));
                break;
            case '\r':
                out.print(F("\\r"));
                break;
            case '\t':
                out.print(F("\\t"));
                break;
            default:
                if (c < 0x20U) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
                    out.print(buf);
                } else {
                    out.write(static_cast<char>(c));
                }
                break;
        }
    }
    out.print('"');
}

static void handleWifiConnectPost(AsyncWebServerRequest* req) {
    if (!configIsApMode()) {
        if (!webAuthIsAuthenticated(req)) {
            req->redirect(F("/auth"));
            return;
        }
        if (!webAuthValidateCsrfPost(req)) {
            req->redirect(F("/wifi"));
            return;
        }
    }
    char ssid[33];
    char password[65];
    ssid[0]     = '\0';
    password[0] = '\0';
    if (!parseBodyParam(req, "ssid", ssid, sizeof(ssid)) || ssid[0] == '\0') {
        req->redirect(F("/wifi"));
        return;
    }
    (void)parseBodyParam(req, "password", password, sizeof(password));
    if (configIsApMode()) {
        if (!wlanStartWifiConnectionTest(ssid, password)) {
            req->redirect(F("/wifi"));
            return;
        }
        req->redirect(F("/wifi-testing"));
        return;
    }

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

static void handleWifiConnectStatusGet(AsyncWebServerRequest* req) {
    if (!configIsApMode()) {
        req->send(404);
        return;
    }

    AsyncResponseStream* resp = req->beginResponseStream("application/json");
    if (resp == nullptr) {
        req->send(500);
        return;
    }

    const WlanWifiConnectionTestState tst = wlanGetWifiConnectionTestState();
    const char*                       stStr =
        tst == WlanWifiConnectionTestState::Idle ? "idle" :
            tst == WlanWifiConnectionTestState::Testing ? "testing" :
                                                        tst == WlanWifiConnectionTestState::Ok   ? "ok" :
                                                                                                   "fail";
    char ssid[33]{};
    (void)wlanWifiConnectionTestSsidSnapshot(ssid, sizeof(ssid));

    resp->print(F("{\"state\":"));
    resp->print('"');
    resp->print(stStr);
    resp->print(F("\",\"ssid\":"));
    appendJsonEscapedCStrWifiStatus(*resp, ssid);
    resp->print('}');
    req->send(resp);
}

static void handleWifiConnectCommitPost(AsyncWebServerRequest* req) {
    if (!configIsApMode()) {
        req->redirect(F("/"));
        return;
    }
    /* Capture IP before commit: wlanAbortWifiConnectionTest() disconnects STA, zeroing localIP. */
    const String staIp = WiFi.localIP().toString();
    if (!wlanCommitWifiConnectionTestAndScheduleReboot()) {
        req->redirect(F("/wifi-testing"));
        return;
    }
    streamWifiCommitDonePage(req, staIp.c_str());
}

static void handleWifiConnectAbortPost(AsyncWebServerRequest* req) {
    if (!configIsApMode()) {
        req->redirect(F("/"));
        return;
    }
    wlanAbortWifiConnectionTest();
    req->redirect(F("/wifi"));
}

static void handleWifiStatusGet(AsyncWebServerRequest* req) {
    AsyncResponseStream* resp = req->beginResponseStream("application/json");
    if (resp == nullptr) {
        req->send(500);
        return;
    }
    if (WiFi.status() == WL_CONNECTED && WiFi.localIP()[0] != 0) {
        resp->print(F("{\"connected\":true,\"ssid\":"));
        appendJsonEscapedCStrWifiStatus(*resp, WiFi.SSID().c_str());
        resp->print(F(",\"ip\":"));
        const String ipStr = WiFi.localIP().toString();
        appendJsonEscapedCStrWifiStatus(*resp, ipStr.c_str());
        resp->print(F(",\"rssi\":"));
        resp->print(static_cast<int>(WiFi.RSSI()));
        resp->print('}');
    } else {
        resp->print(F("{\"connected\":false}"));
    }
    req->send(resp);
}

static void handleMqttStatusGet(AsyncWebServerRequest* req) {
    if (configIsApMode()) {
        req->redirect(F("/"));
        return;
    }
    if (webAuthRedirectIfUnauthenticated(req)) {
        return;
    }
    AsyncResponseStream* resp = req->beginResponseStream("application/json");
    if (resp == nullptr) {
        req->send(500);
        return;
    }
    resp->print(mqttIsConnected() ? F("{\"connected\":true}") : F("{\"connected\":false}"));
    req->send(resp);
}

static void handleUpdateCheckPost(AsyncWebServerRequest* req) {
    if (!webAuthIsAuthenticated(req)) {
        req->redirect(F("/auth"));
        return;
    }
    if (!webAuthValidateCsrfPost(req)) {
        req->redirect(F("/update"));
        return;
    }
    otaQueueGithubCheck();
    streamSimpleDonePage(req, "Update", "Checking GitHub; an update may follow…");
}

static void handleRebootPost(AsyncWebServerRequest* req) {
    if (!webAuthIsAuthenticated(req)) {
        req->redirect(F("/auth"));
        return;
    }
    if (!webAuthValidateCsrfPost(req)) {
        req->redirect(F("/"));
        return;
    }
    g_rebootRequested.store(true, std::memory_order_release);
    streamSimpleDonePage(req, "Reboot", "Rebooting…");
}

static void handleMqttPost(AsyncWebServerRequest* req) {
    if (!webAuthIsAuthenticated(req)) {
        req->redirect(F("/auth"));
        return;
    }
    if (!webAuthValidateCsrfPost(req)) {
        req->redirect(F("/mqtt"));
        return;
    }

    MqttConfig pending{};
    mqttCfgSnapshot(&pending);

    if (req->hasParam("mqtt_server", true)) {
        const AsyncWebParameter* p = req->getParam("mqtt_server", true);
        if (p != nullptr) {
            strlcpy(pending.server, p->value().c_str(), sizeof(pending.server));
        }
    }
    if (req->hasParam("mqtt_port", true)) {
        const AsyncWebParameter* p = req->getParam("mqtt_port", true);
        if (p != nullptr) {
            errno          = 0;
            char* endPtr   = nullptr;
            const long v   = strtol(p->value().c_str(), &endPtr, 10);
            const bool bad = (errno == ERANGE) || (endPtr == p->value().c_str()) || (*endPtr != '\0');
            if (bad) {
                req->redirect(F("/mqtt"));
                return;
            }
            pending.port = normalizeMqttPort(static_cast<int>(v));
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
        if (p != nullptr && p->value().length() > 0) {
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
    if (!mqttTopicSyntaxOk(pending.server, sizeof(pending.server))
        || !mqttTopicSyntaxOk(pending.topicPub, sizeof(pending.topicPub))
        || !mqttTopicSyntaxOk(pending.topicSub, sizeof(pending.topicSub))) {
        ESP_LOGW(TAG, "MQTT: ungueltige Topics oder leerer Broker");
        req->redirect(F("/mqtt"));
        return;
    }
    mqttCfgStorePending(&pending);
    g_mqttApplyPending.store(true, std::memory_order_release);
    req->redirect(F("/mqtt?saved=1"));
}

static void handleSettingsPost(AsyncWebServerRequest* req) {
    if (!webAuthIsAuthenticated(req)) {
        req->redirect(F("/auth"));
        return;
    }
    if (!webAuthValidateCsrfPost(req)) {
        req->redirect(F("/settings"));
        return;
    }
    uint8_t days = 7;
    if (req->hasParam("reset_days", true)) {
        const AsyncWebParameter* p = req->getParam("reset_days", true);
        if (p != nullptr) {
            const int v = p->value().toInt();
            days          = (v >= 0 && v <= 30) ? static_cast<uint8_t>(v) : 7;
        }
    }
    portENTER_CRITICAL(&s_settingsPendingMux);
    s_pendingResetDays     = days;
    s_pendingWebAuthEnabled = req->hasParam("auth_enabled", true);
    portEXIT_CRITICAL(&s_settingsPendingMux);
    g_settingsApplyPending.store(true, std::memory_order_release);
    req->redirect(F("/settings?saved=1"));
}

void webAdminRegisterRoutes() {
    if (g_routesRegistered) {
        return;
    }
    g_routesRegistered = true;

    webAuthInit();
    AsyncWebServer& ws = webAdminWebServer();
    webAuthRegisterRoutes(ws);

    ws.on("/", HTTP_GET, [](AsyncWebServerRequest* rq) {
        if (webAuthRedirectIfUnauthenticated(rq)) {
            return;
        }
        streamDashboard(rq);
    });
    ws.on("/wifi", HTTP_GET, [](AsyncWebServerRequest* rq) {
        streamWifiPage(rq);
    });
    ws.on("/wifi-testing", HTTP_GET, [](AsyncWebServerRequest* rq) { streamWifiTestingPage(rq); });
    ws.on("/wifi-scan", HTTP_GET, [](AsyncWebServerRequest* rq) {
        handleWifiScanJson(rq);
    });
    ws.on("/wifi-status", HTTP_GET, [](AsyncWebServerRequest* rq) {
        handleWifiStatusGet(rq);
    });
    ws.on("/wifi-connect-status", HTTP_GET,
        [](AsyncWebServerRequest* rq) { handleWifiConnectStatusGet(rq); });
    ws.on("/wifi-connect-commit", HTTP_POST,
        [](AsyncWebServerRequest* rq) { handleWifiConnectCommitPost(rq); });
    ws.on("/wifi-connect-abort", HTTP_POST,
        [](AsyncWebServerRequest* rq) { handleWifiConnectAbortPost(rq); });
    ws.on("/wifi-connect", HTTP_POST, [](AsyncWebServerRequest* rq) {
        handleWifiConnectPost(rq);
    });
    ws.on("/update", HTTP_GET, [](AsyncWebServerRequest* rq) {
        if (configIsApMode()) {
            rq->redirect(F("/"));
            return;
        }
        if (webAuthRedirectIfUnauthenticated(rq)) {
            return;
        }
        streamUpdatePage(rq);
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
        if (webAuthRedirectIfUnauthenticated(rq)) {
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
    ws.on("/mqtt-status", HTTP_GET, [](AsyncWebServerRequest* rq) {
        handleMqttStatusGet(rq);
    });

    ws.on("/settings", HTTP_GET, [](AsyncWebServerRequest* rq) {
        if (configIsApMode()) {
            rq->redirect(F("/"));
            return;
        }
        if (webAuthRedirectIfUnauthenticated(rq)) {
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

void webAdminScheduleWifiConfiguredReboot() {
    g_wifiConnectRequested.store(true, std::memory_order_release);
}

void webAdminLoop() {
    webAuthLoop();

    if (g_settingsApplyPending.exchange(false, std::memory_order_acq_rel)) {
        uint8_t daysApply = 7;
        bool    authApply = false;
        portENTER_CRITICAL(&s_settingsPendingMux);
        daysApply = s_pendingResetDays;
        authApply = s_pendingWebAuthEnabled;
        portEXIT_CRITICAL(&s_settingsPendingMux);
        configSetResetPeriodDays(daysApply);
        configSetWebAuthEnabled(authApply);
    }

    if (g_mqttApplyPending.exchange(false, std::memory_order_acq_rel)) {
        mqttCfgApplyPendingToActive();
        saveMQTTConfig();
        mqttDisconnect();
        mqttSetup();
        mqttPostponeConnect(3000UL);
        requestHeartRedraw();
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
