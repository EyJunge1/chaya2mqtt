#include <Arduino.h>

#include "admin_globals.h"
#include "admin_routes.h"

#include "config/app_config.h"
#include "constants.h"
#include "ip_format.h"
#include "log_tag.h"
#include "wifi/wlan.h"
#include "wifi/test.h"
#include "web_middleware.h"
#include "web_utils.h"

#include "pages.h"

#include <ESPAsyncWebServer.h>

#include <cstdio>
#include <cstring>
#include <esp_log.h>

DEFINE_LOG_TAG("WIFIADM");

static void handleWifiConnectPost(AsyncWebServerRequest* req) {
    char ssid[kWifiSsidMaxLen];
    char password[kWifiPassMaxLen];
    ssid[0]     = '\0';
    password[0] = '\0';
    if (!adminParseBodyParam(req, "ssid", ssid, sizeof(ssid)) || ssid[0] == '\0') {
        ESP_LOGW(TAG, "Wi-Fi connect POST rejected: missing SSID");
        req->redirect(F("/wifi"));
        return;
    }
    (void)adminParseBodyParam(req, "password", password, sizeof(password));
    if (configIsApMode()) {
        if (!wlanStartWifiConnectionTest(ssid, password)) {
            ESP_LOGW(TAG, "Wi-Fi connect test could not start (SSID=%s)", ssid);
            req->redirect(F("/wifi"));
            return;
        }
        ESP_LOGI(TAG, "Wi-Fi connect test started (setup AP), SSID=%s", ssid);
        req->redirect(F("/wifi-testing"));
        return;
    }

    if (!configSaveWiFiCredentials(ssid, password)) {
        ESP_LOGE(TAG, "Failed to persist Wi-Fi credentials");
        req->redirect(F("/wifi"));
        return;
    }
    g_webAdminWifiReconnectRequested.store(true, std::memory_order_release);
    ESP_LOGI(TAG, "Wi-Fi credentials saved; STA reconnect scheduled");

    char doneMsg[200];
    snprintf(doneMsg, sizeof(doneMsg),
             "Wi-Fi saved — device restarting.<br>"
             "Configure MQTT and more at "
             "<strong>%s</strong> (same network).",
             kDeviceHttpOrigin);
    streamSimpleDonePage(req, "Wi-Fi", doneMsg);
}

static void handleWifiConnectStatusGet(AsyncWebServerRequest* req) {
    const WlanWifiConnectionTestState tst = wlanGetWifiConnectionTestState();
    const char*                       stStr =
        tst == WlanWifiConnectionTestState::Idle ? "idle" :
        tst == WlanWifiConnectionTestState::Testing ? "testing" :
        tst == WlanWifiConnectionTestState::Ok      ? "ok" :
                                                      "fail";
    char ssid[kWifiSsidMaxLen]{};
    (void)wlanWifiConnectionTestSsidSnapshot(ssid, sizeof(ssid));

    char   body[256]{};
    size_t pos = 0;
    const int h =
        snprintf(body, sizeof(body), "{\"state\":\"%s\",\"ssid\":", stStr);
    if (h < 0 || static_cast<size_t>(h) >= sizeof(body)) {
        ESP_LOGE(TAG, "/wifi-connect-status JSON build overflow");
        req->send(500);
        return;
    }
    pos = static_cast<size_t>(h);
    if (!appendJsonStringQuotedEscaped(ssid, body, sizeof(body), &pos)) {
        ESP_LOGE(TAG, "/wifi-connect-status SSID escape failed");
        req->send(500);
        return;
    }
    if (pos + 2U > sizeof(body)) {
        ESP_LOGE(TAG, "/wifi-connect-status JSON too large");
        req->send(500);
        return;
    }
    body[pos++] = '}';
    body[pos]   = '\0';
    req->send(200, "application/json", body);
}

static void handleWifiConnectCommitPost(AsyncWebServerRequest* req) {
    // Snapshot IP before commit tears down STA.
    char staIp[16]{};
    (void)wlanReadStaLocalIpForCommit(staIp, sizeof(staIp));
    if (!wlanCommitWifiConnectionTestAndScheduleReboot()) {
        ESP_LOGW(TAG, "Wi-Fi setup commit refused (state not OK?)");
        req->redirect(F("/wifi-testing"));
        return;
    }
    ESP_LOGI(TAG, "Wi-Fi setup credentials committed; device reboot scheduled");
    streamWifiCommitDonePage(req, staIp);
}

static void handleWifiConnectAbortPost(AsyncWebServerRequest* req) {
    ESP_LOGI(TAG, "Wi-Fi setup connect test aborted by user");
    wlanAbortWifiConnectionTest();
    req->redirect(F("/wifi"));
}

static void handleWifiStatusGet(AsyncWebServerRequest* req) {
    bool connected = false;
    char ipStr[16]{};
    char ssidBuf[kWifiSsidMaxLen]{};
    int  rssi = 0;
    wlanFillStaLinkSnapshot(&connected, ipStr, sizeof(ipStr), ssidBuf, sizeof(ssidBuf), &rssi);
    if (!connected) {
        req->send(200, "application/json", "{\"connected\":false}");
        return;
    }
    char   body[384]{};
    size_t pos = 0;
    const int h = snprintf(body, sizeof(body),
                           "{\"connected\":true,\"ip\":\"%s\",\"rssi\":%d,\"ssid\":", ipStr, rssi);
    if (h < 0 || static_cast<size_t>(h) >= sizeof(body)) {
        ESP_LOGE(TAG, "/wifi-status JSON prefix overflow");
        req->send(500);
        return;
    }
    pos = static_cast<size_t>(h);
    if (!appendJsonStringQuotedEscaped(ssidBuf, body, sizeof(body), &pos)) {
        ESP_LOGE(TAG, "/wifi-status SSID escape failed");
        req->send(500);
        return;
    }
    if (pos + 2U > sizeof(body)) {
        ESP_LOGE(TAG, "/wifi-status JSON too large");
        req->send(500);
        return;
    }
    body[pos++] = '}';
    body[pos]   = '\0';
    req->send(200, "application/json", body);
}

void adminRoutesRegisterWifi(AsyncWebServer& ws) {
    {
        AsyncCallbackWebHandler& h =
            ws.on("/wifi", HTTP_GET, [](AsyncWebServerRequest* rq) { streamWifiPage(rq); });
        h.addMiddleware(mwWifiInfoOrApOpenGet());
    }
    {
        AsyncCallbackWebHandler& h = ws.on(
            "/wifi-testing", HTTP_GET, [](AsyncWebServerRequest* rq) { streamWifiTestingPage(rq); });
        h.addMiddleware(mwWifiInfoOrApOpenGet());
    }
    {
        AsyncCallbackWebHandler& h =
            ws.on("/wifi-scan", HTTP_GET, [](AsyncWebServerRequest* rq) { handleWifiScanJson(rq); });
        h.addMiddleware(mwWifiInfoOrApOpenGet());
    }
    {
        AsyncCallbackWebHandler& h =
            ws.on("/wifi-status", HTTP_GET, [](AsyncWebServerRequest* rq) { handleWifiStatusGet(rq); });
        h.addMiddleware(mwWifiInfoOrApOpenGet());
    }
    {
        AsyncCallbackWebHandler& h = ws.on("/wifi-connect-status", HTTP_GET,
            [](AsyncWebServerRequest* rq) {
                handleWifiConnectStatusGet(rq);
            });
        h.addMiddleware(mwRequireApMode());
    }
    {
        AsyncCallbackWebHandler& h = ws.on("/wifi-connect-commit", HTTP_POST,
            [](AsyncWebServerRequest* rq) {
                handleWifiConnectCommitPost(rq);
            });
        h.addMiddleware(mwRequireApMode());
        h.addMiddleware(mwApPostCsrfRedirect("/wifi-testing"));
    }
    {
        AsyncCallbackWebHandler& h = ws.on("/wifi-connect-abort", HTTP_POST,
            [](AsyncWebServerRequest* rq) {
                handleWifiConnectAbortPost(rq);
            });
        h.addMiddleware(mwRequireApMode());
        h.addMiddleware(mwApPostCsrfRedirect("/wifi"));
    }
    {
        AsyncCallbackWebHandler& h = ws.on("/wifi-connect", HTTP_POST, [](AsyncWebServerRequest* rq) {
            handleWifiConnectPost(rq);
        });
        h.addMiddleware(mwWifiConnectPostGuard());
    }
}
