#include <Arduino.h>

#include "admin_globals.h"
#include "admin_routes.h"

#include "app_config.h"
#include "constants.h"
#include "wlan.h"
#include "web_utils.h"

#include "auth.h"
#include "pages.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>

#include <cstdio>

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
    char ssid[kWifiSsidMaxLen];
    char password[kWifiPassMaxLen];
    ssid[0]     = '\0';
    password[0] = '\0';
    if (!adminParseBodyParam(req, "ssid", ssid, sizeof(ssid)) || ssid[0] == '\0') {
        req->redirect(F("/wifi"));
        return;
    }
    (void)adminParseBodyParam(req, "password", password, sizeof(password));
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
    g_webAdminWifiReconnectRequested.store(true, std::memory_order_release);

    char doneMsg[200];
    snprintf(doneMsg, sizeof(doneMsg),
             "Wi-Fi saved — device restarting.<br>"
             "Configure MQTT and more at "
             "<strong>%s</strong> (same network).",
             kDeviceHttpOrigin);
    streamSimpleDonePage(req, "Wi-Fi", doneMsg);
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
                                                        tst == WlanWifiConnectionTestState::Ok   ? "ok"
                                                                                                 : "fail";
    char ssid[kWifiSsidMaxLen]{};
    (void)wlanWifiConnectionTestSsidSnapshot(ssid, sizeof(ssid));

    resp->print(F("{\"state\":"));
    resp->print('"');
    resp->print(stStr);
    resp->print(F("\",\"ssid\":"));
    appendJsonEscapedCStr(*resp, ssid);
    resp->print('}');
    req->send(resp);
}

static void handleWifiConnectCommitPost(AsyncWebServerRequest* req) {
    if (!configIsApMode()) {
        req->redirect(F("/"));
        return;
    }
    /* Capture IP before commit: wlanAbortWifiConnectionTest() disconnects STA. */
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
        appendJsonEscapedCStr(*resp, WiFi.SSID().c_str());
        resp->print(F(",\"ip\":"));
        const String ipStr = WiFi.localIP().toString();
        appendJsonEscapedCStr(*resp, ipStr.c_str());
        resp->print(F(",\"rssi\":"));
        resp->print(static_cast<int>(WiFi.RSSI()));
        resp->print('}');
    } else {
        resp->print(F("{\"connected\":false}"));
    }
    req->send(resp);
}

void adminRoutesRegisterWifi(AsyncWebServer& ws) {
    ws.on("/wifi", HTTP_GET,
        [](AsyncWebServerRequest* rq) {
            streamWifiPage(rq);
        });
    ws.on("/wifi-testing", HTTP_GET,
        [](AsyncWebServerRequest* rq) {
            streamWifiTestingPage(rq);
        });
    ws.on("/wifi-scan", HTTP_GET, [](AsyncWebServerRequest* rq) {
        handleWifiScanJson(rq);
    });
    ws.on("/wifi-status", HTTP_GET, [](AsyncWebServerRequest* rq) {
        handleWifiStatusGet(rq);
    });
    ws.on("/wifi-connect-status", HTTP_GET,
        [](AsyncWebServerRequest* rq) {
            handleWifiConnectStatusGet(rq);
        });
    ws.on("/wifi-connect-commit", HTTP_POST,
        [](AsyncWebServerRequest* rq) {
            handleWifiConnectCommitPost(rq);
        });
    ws.on("/wifi-connect-abort", HTTP_POST,
        [](AsyncWebServerRequest* rq) {
            handleWifiConnectAbortPost(rq);
        });
    ws.on("/wifi-connect", HTTP_POST, [](AsyncWebServerRequest* rq) {
        handleWifiConnectPost(rq);
    });
}
