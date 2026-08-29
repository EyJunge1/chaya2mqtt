#include <Arduino.h>

#include "../admin_globals.h"
#include "../admin_json.h"
#include "admin_routes_api_internal.h"

#include "async/event_types.h"
#include "async/task_handles.h"
#include "config/app_config.h"
#include "config/version.h"
#include "constants.h"
#include "identity/device_identity.h"
#include "heart/counter.h"
#include "battery/battery.h"
#include "mqtt/config.h"
#include "mqtt/mqtt.h"
#include "ota/ota.h"
#include "util/log_tag.h"
#include "web/csrf.h"
#include "web/deferred_reboot.h"
#include "web/web_middleware.h"
#include "web/web_utils.h"
#include "wifi/test.h"
#include "wifi/wlan.h"
#include "wifi/wlan_config.h"

#include <ESPAsyncWebServer.h>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <esp_log.h>

DEFINE_LOG_TAG("WEBAPI");

bool appendWifiRuntimeFields(char* body, size_t bodyLen, size_t* pos, const char* ip,
                             const char* gateway, const char* netmask, const char* dns1,
                             const char* dns2, int rssi) {
    if (body == nullptr || pos == nullptr || *pos >= bodyLen) {
        return false;
    }
    const int n = snprintf(
        body + *pos, bodyLen - *pos,
        ",\"ip\":\"%s\",\"gateway\":\"%s\",\"netmask\":\"%s\",\"dns1\":\"%s\",\"dns2\":\"%s\","
        "\"rssi\":%d",
        ip != nullptr ? ip : "", gateway != nullptr ? gateway : "",
        netmask != nullptr ? netmask : "", dns1 != nullptr ? dns1 : "",
        dns2 != nullptr ? dns2 : "", rssi);
    if (n < 0 || *pos + static_cast<size_t>(n) >= bodyLen) {
        return false;
    }
    *pos += static_cast<size_t>(n);
    return true;
}

void handleApiWifiStatusGet(AsyncWebServerRequest* req) {
    bool connected = false;
    char ssidBuf[kWifiSsidMaxLen]{};
    char ipStr[kIpv4StrMaxLen]{};
    char gateway[kIpv4StrMaxLen]{};
    char netmask[kIpv4StrMaxLen]{};
    char dns1[kIpv4StrMaxLen]{};
    char dns2[kIpv4StrMaxLen]{};
    int  rssi = 0;
    wlanFillStaNetSnapshot(&connected, ssidBuf, sizeof(ssidBuf), ipStr, sizeof(ipStr), gateway,
                           sizeof(gateway), netmask, sizeof(netmask), dns1, sizeof(dns1), dns2,
                           sizeof(dns2), &rssi);
    if (!connected) {
        webSendJson(req, 200, "{\"connected\":false}");
        return;
    }
    char   body[640]{};
    size_t pos = 0;
    const int h = snprintf(body, sizeof(body), "{\"connected\":true,\"ssid\":");
    if (h < 0 || static_cast<size_t>(h) >= sizeof(body)) {
        webSendEmpty(req, 500);
        return;
    }
    pos = static_cast<size_t>(h);
    if (!appendJsonStringQuotedEscaped(ssidBuf, body, sizeof(body), &pos)) {
        webSendEmpty(req, 500);
        return;
    }
    if (!appendWifiRuntimeFields(body, sizeof(body), &pos, ipStr, gateway, netmask, dns1, dns2,
                                 rssi)) {
        webSendEmpty(req, 500);
        return;
    }
    if (pos + 2U > sizeof(body)) {
        webSendEmpty(req, 500);
        return;
    }
    body[pos++] = '}';
    body[pos]   = '\0';
    webSendJson(req, 200, body);
}

bool fillWifiConfigJson(char* body, size_t bodyLen, const WlanConfig& cfg) {
    size_t pos = 0;
    int n = snprintf(body, bodyLen, "{\"ssid\":");
    if (n < 0 || static_cast<size_t>(n) >= bodyLen) {
        return false;
    }
    pos = static_cast<size_t>(n);
    if (!appendJsonStringQuotedEscaped(cfg.ssid, body, bodyLen, &pos)) {
        return false;
    }
    n = snprintf(body + pos, bodyLen - pos,
                 ",\"mode\":\"%s\",\"ip\":\"%s\",\"gateway\":\"%s\",\"netmask\":\"%s\","
                 "\"dns1\":\"%s\",\"dns2\":\"%s\",\"ntp1\":",
                 cfg.mode == WlanIpMode::Static ? "static" : "dhcp", cfg.ip, cfg.gateway,
                 cfg.netmask, cfg.dns1, cfg.dns2);
    if (n < 0 || pos + static_cast<size_t>(n) >= bodyLen) {
        return false;
    }
    pos += static_cast<size_t>(n);
    if (!appendJsonStringQuotedEscaped(cfg.ntp1, body, bodyLen, &pos)) {
        return false;
    }
    n = snprintf(body + pos, bodyLen - pos, ",\"ntp2\":");
    if (n < 0 || pos + static_cast<size_t>(n) >= bodyLen) {
        return false;
    }
    pos += static_cast<size_t>(n);
    if (!appendJsonStringQuotedEscaped(cfg.ntp2, body, bodyLen, &pos)) {
        return false;
    }
    if (pos + 2U > bodyLen) {
        return false;
    }
    body[pos++] = '}';
    body[pos]   = '\0';
    return true;
}

void handleApiWifiConfigGet(AsyncWebServerRequest* req) {
    WlanConfig cfg{};
    if (!wlanLoadConfigFromNvs(&cfg)) {
        wlanConfigClear(&cfg);
    }
    char body[768]{};
    if (!fillWifiConfigJson(body, sizeof(body), cfg)) {
        webSendEmpty(req, 500);
        return;
    }
    webSendJson(req, 200, body);
}

bool parseWifiConfigFromRequest(AsyncWebServerRequest* req, WlanConfig* cfg, const char** err) {
    if (req == nullptr || cfg == nullptr || err == nullptr) {
        return false;
    }
    *err = "ssid";
    wlanConfigClear(cfg);

    if (!adminParseBodyParam(req, "ssid", cfg->ssid, sizeof(cfg->ssid)) || cfg->ssid[0] == '\0'
        || !wifiSsidSyntaxOk(cfg->ssid, sizeof(cfg->ssid))) {
        *err = "ssid";
        return false;
    }
    auto parseOptional = [&](const char* name, char* out, size_t outLen) {
        if (!req->hasParam(name, true)) {
            return true;
        }
        if (!adminParseBodyParam(req, name, out, outLen)) {
            *err = name;
            return false;
        }
        return true;
    };
    if (!parseOptional("password", cfg->pass, sizeof(cfg->pass))) {
        return false;
    }

    char modeBuf[12]{};
    if (req->hasParam("mode", true)) {
        if (!adminParseBodyParam(req, "mode", modeBuf, sizeof(modeBuf))) {
            *err = "mode";
            return false;
        }
        if (strcmp(modeBuf, "static") == 0) {
            cfg->mode = WlanIpMode::Static;
        } else if (strcmp(modeBuf, "dhcp") == 0) {
            cfg->mode = WlanIpMode::Dhcp;
        } else {
            *err = "mode";
            return false;
        }
    } else {
        cfg->mode = WlanIpMode::Dhcp;
    }

    if (!parseOptional("ip", cfg->ip, sizeof(cfg->ip))
        || !parseOptional("gateway", cfg->gateway, sizeof(cfg->gateway))
        || !parseOptional("netmask", cfg->netmask, sizeof(cfg->netmask))
        || !parseOptional("dns1", cfg->dns1, sizeof(cfg->dns1))
        || !parseOptional("dns2", cfg->dns2, sizeof(cfg->dns2))
        || !parseOptional("ntp1", cfg->ntp1, sizeof(cfg->ntp1))
        || !parseOptional("ntp2", cfg->ntp2, sizeof(cfg->ntp2))) {
        return false;
    }

    if (cfg->mode == WlanIpMode::Dhcp) {
        cfg->ip[0] = cfg->gateway[0] = cfg->netmask[0] = '\0';
    }

    const char* validationErr = wlanConfigValidate(cfg);
    if (validationErr != nullptr) {
        *err = validationErr;
        return false;
    }
    *err = nullptr;
    return true;
}

void handleApiWifiScanGet(AsyncWebServerRequest* req) {
    if (!wlanWifiScanCacheReady()) {
        wlanRequestWifiScanRefresh();
        webSendEmpty(req, 202);
        return;
    }
    AsyncResponseStream* resp = beginResponseStreamOr500(req, "application/json");
    if (resp == nullptr) {
        return;
    }
    const size_t n = wlanWifiScanCachedCount();
    resp->print('[');
    for (size_t i = 0; i < n; ++i) {
        WlanScanRow row{};
        if (!wlanWifiScanCopyRowAt(i, &row)) {
            break;
        }
        if (i > 0U) {
            resp->print(',');
        }
        resp->print(F("{\"ssid\":"));
        appendJsonEscapedCStr(*resp, row.ssid);
        resp->print(F(",\"rssi\":"));
        resp->print(row.rssi);
        resp->print(row.open ? F(",\"open\":true}") : F(",\"open\":false}"));
    }
    resp->print(']');
    req->send(resp);
    wlanRequestWifiScanRefresh();
}

void handleApiWifiConnectPost(AsyncWebServerRequest* req) {
    WlanConfig cfg{};
    const char* err = nullptr;
    if (!parseWifiConfigFromRequest(req, &cfg, &err)) {
        sendErr(req, 400, err != nullptr ? err : "ssid");
        return;
    }
    if (configIsApMode()) {
        if (!wlanStartWifiConnectionTest(cfg)) {
            sendErr(req, 503, "test_start");
            return;
        }
        ESP_LOGI(TAG, "WiFi connect test started ssid=%s", cfg.ssid);
        sendOk(req, 200, "\"next\":\"/wifi-testing\"");
        return;
    }
    if (!wlanSaveConfigToNvs(cfg)) {
        sendErr(req, 500, "save");
        return;
    }
    ESP_LOGI(TAG, "WiFi config saved (STA) ssid=%s — rebooting", cfg.ssid);
    deferredRebootAfterWifiSave();
    sendOk(req, 200, "\"message\":\"saved_rebooting\"");
}

void handleApiWifiConnectStatusGet(AsyncWebServerRequest* req) {
    const WlanWifiConnectionTestState tst = wlanGetWifiConnectionTestState();
    const char* stStr = tst == WlanWifiConnectionTestState::Idle      ? "idle"
                        : tst == WlanWifiConnectionTestState::Testing ? "testing"
                        : tst == WlanWifiConnectionTestState::Ok      ? "ok"
                                                                      : "fail";
    char ssid[kWifiSsidMaxLen]{};
    (void)wlanWifiConnectionTestSsidSnapshot(ssid, sizeof(ssid));
    char   body[256]{};
    size_t pos = 0;
    const int h = snprintf(body, sizeof(body), "{\"state\":\"%s\",\"ssid\":", stStr);
    if (h < 0 || static_cast<size_t>(h) >= sizeof(body)) {
        webSendEmpty(req, 500);
        return;
    }
    pos = static_cast<size_t>(h);
    if (!appendJsonStringQuotedEscaped(ssid, body, sizeof(body), &pos)) {
        webSendEmpty(req, 500);
        return;
    }
    if (pos + 2U > sizeof(body)) {
        webSendEmpty(req, 500);
        return;
    }
    body[pos++] = '}';
    body[pos]   = '\0';
    webSendJson(req, 200, body);
}

void handleApiWifiConnectCommitPost(AsyncWebServerRequest* req) {
    char staIp[16]{};
    if (!wlanReadStaLocalIpForCommit(staIp, sizeof(staIp))) {
        sendErr(req, 400, "not_connected");
        return;
    }
    if (!wlanCommitWifiConnectionTestAndScheduleReboot()) {
        sendErr(req, 400, "not_ok");
        return;
    }
    char extra[96]{};
    const int n = snprintf(extra, sizeof(extra),
                           "\"message\":\"committed\",\"next\":\"http://%s/\"", staIp);
    if (n < 0 || static_cast<size_t>(n) >= sizeof(extra)) {
        sendOk(req, 200, "\"message\":\"committed\"");
        return;
    }
    sendOk(req, 200, extra);
}

void handleApiWifiConnectAbortPost(AsyncWebServerRequest* req) {
    wlanAbortWifiConnectionTest();
    sendOk(req, 200, "\"next\":\"/wifi\"");
}


void adminRoutesRegisterApiWifi(AsyncWebServer& ws) {
    {
        AsyncCallbackWebHandler& h = ws.on("/api/wifi/status", HTTP_GET,
                                           [](AsyncWebServerRequest* rq) { handleApiWifiStatusGet(rq); });
        h.addMiddleware(mwRequireAllowedHost());
    }
    {
        AsyncCallbackWebHandler& h = ws.on("/api/wifi/config", HTTP_GET,
                                           [](AsyncWebServerRequest* rq) { handleApiWifiConfigGet(rq); });
        h.addMiddleware(mwRequireAllowedHost());
    }
    {
        AsyncCallbackWebHandler& h = ws.on("/api/wifi/scan", HTTP_GET,
                                           [](AsyncWebServerRequest* rq) { handleApiWifiScanGet(rq); });
        h.addMiddleware(mwRequireAllowedHost());
    }
    {
        AsyncCallbackWebHandler& h = ws.on("/api/wifi/connect", HTTP_POST,
                                           [](AsyncWebServerRequest* rq) { handleApiWifiConnectPost(rq); });
        h.addMiddleware(mwApiPostCsrf());
    }
    {
        AsyncCallbackWebHandler& h = ws.on(
            "/api/wifi/connect-status", HTTP_GET,
            [](AsyncWebServerRequest* rq) { handleApiWifiConnectStatusGet(rq); });
        h.addMiddleware(mwApiApMode());
    }
    {
        AsyncCallbackWebHandler& h = ws.on(
            "/api/wifi/connect-commit", HTTP_POST,
            [](AsyncWebServerRequest* rq) { handleApiWifiConnectCommitPost(rq); });
        h.addMiddleware(mwApiApPostCsrf());
    }
    {
        AsyncCallbackWebHandler& h = ws.on(
            "/api/wifi/connect-abort", HTTP_POST,
            [](AsyncWebServerRequest* rq) { handleApiWifiConnectAbortPost(rq); });
        h.addMiddleware(mwApiApPostCsrf());
    }
}
