#include <Arduino.h>

#include "../admin_globals.h"
#include "admin_routes_api_internal.h"

#include "async/system_lifecycle.h"
#include "config/app_config.h"
#include "constants.h"
#include "ota/ota.h"
#include "util/format_buf.h"
#include "util/log_tag.h"
#include "web/deferred_reboot.h"
#include "web/web_utils.h"
#include "wifi/test.h"
#include "wifi/wlan.h"
#include "wifi/wlan_config.h"

#include <ESPAsyncWebServer.h>
#include <cstring>
#include <esp_log.h>
#include <span>

DEFINE_LOG_TAG("WEBAPI");

void fillWifiRuntimeFields(JsonObject obj, const char *ip, const char *gateway, const char *netmask, const char *dns1,
                           const char *dns2, int rssi) {
    obj["ip"] = ip != nullptr ? ip : "";
    obj["gateway"] = gateway != nullptr ? gateway : "";
    obj["netmask"] = netmask != nullptr ? netmask : "";
    obj["dns1"] = dns1 != nullptr ? dns1 : "";
    obj["dns2"] = dns2 != nullptr ? dns2 : "";
    obj["rssi"] = rssi;
}

void fillWifiConfigJson(JsonObject obj, const WlanConfig &cfg) {
    obj["ssid"] = cfg.ssid;
    obj["mode"] = cfg.mode == WlanIpMode::Static ? "static" : "dhcp";
    obj["ip"] = cfg.ip;
    obj["gateway"] = cfg.gateway;
    obj["netmask"] = cfg.netmask;
    obj["dns1"] = cfg.dns1;
    obj["dns2"] = cfg.dns2;
    obj["ntp1"] = cfg.ntp1;
    obj["ntp2"] = cfg.ntp2;
}

void handleApiWifiConfigGet(AsyncWebServerRequest *req) {
    WlanConfig cfg{};
    if (!wlanCopyCachedConfig(&cfg)) {
        wlanConfigClear(&cfg);
    }
    JsonDocument doc;
    fillWifiConfigJson(doc.to<JsonObject>(), cfg);
    webSendJsonDoc(req, 200, doc);
}

bool parseWifiConfigFromJson(JsonVariantConst json, WlanConfig *cfg, const char **err, bool *passwordPresent) {
    if (cfg == nullptr || err == nullptr) {
        return false;
    }
    if (passwordPresent != nullptr) {
        *passwordPresent = false;
    }
    *err = "ssid";
    wlanConfigClear(cfg);

    const auto ssidField = adminOptionalJsonString(json, "ssid", cfg->ssid, sizeof(cfg->ssid));
    if (!ssidField.has_value() || !ssidField->has_value() || cfg->ssid[0] == '\0' ||
        !wifiSsidSyntaxOk(cfg->ssid, sizeof(cfg->ssid))) {
        *err = "ssid";
        return false;
    }

    auto parseOptional = [&](const char *name, char *out, size_t outLen) {
        const auto field = adminOptionalJsonString(json, name, out, outLen);
        if (!field.has_value()) {
            *err = name;
            return false;
        }
        return true;
    };
    const auto passwordField = adminOptionalJsonString(json, "password", cfg->pass, sizeof(cfg->pass));
    if (!passwordField.has_value()) {
        *err = "password";
        return false;
    }
    if (passwordField->has_value() && passwordPresent != nullptr) {
        *passwordPresent = true;
    }

    char modeBuf[12]{};
    const auto modeField = adminOptionalJsonString(json, "mode", modeBuf, sizeof(modeBuf));
    if (!modeField.has_value()) {
        *err = "mode";
        return false;
    }
    if (!modeField->has_value()) {
        cfg->mode = WlanIpMode::Dhcp;
    } else if (strcmp(modeBuf, "static") == 0) {
        cfg->mode = WlanIpMode::Static;
    } else if (strcmp(modeBuf, "dhcp") == 0) {
        cfg->mode = WlanIpMode::Dhcp;
    } else {
        *err = "mode";
        return false;
    }

    if (!parseOptional("ip", cfg->ip, sizeof(cfg->ip)) || !parseOptional("gateway", cfg->gateway, sizeof(cfg->gateway)) ||
        !parseOptional("netmask", cfg->netmask, sizeof(cfg->netmask)) || !parseOptional("dns1", cfg->dns1, sizeof(cfg->dns1)) ||
        !parseOptional("dns2", cfg->dns2, sizeof(cfg->dns2)) || !parseOptional("ntp1", cfg->ntp1, sizeof(cfg->ntp1)) ||
        !parseOptional("ntp2", cfg->ntp2, sizeof(cfg->ntp2))) {
        return false;
    }

    if (cfg->mode == WlanIpMode::Dhcp) {
        cfg->ip[0] = cfg->gateway[0] = cfg->netmask[0] = '\0';
    }

    const char *validationErr = wlanConfigValidate(cfg);
    if (validationErr != nullptr) {
        *err = validationErr;
        return false;
    }
    *err = nullptr;
    return true;
}

void handleApiWifiScanGet(AsyncWebServerRequest *req) {
    // Read-only: POST /api/wifi/scan kicks. Polling GETs must not start a sweep.
    const WlanWifiScanStatus st = wlanWifiScanStatus();
    if (st != WlanWifiScanStatus::Ready) {
        const char *status = st == WlanWifiScanStatus::Pending ? "pending" : st == WlanWifiScanStatus::Failed ? "failed" : "idle";
        JsonDocument doc;
        doc["status"] = status;
        webSendJsonDoc(req, 200, doc);
        return;
    }
    JsonDocument doc;
    doc["status"] = "ready";
    JsonArray aps = doc["aps"].to<JsonArray>();
    WlanScanRow rows[kWlanWifiScanCacheMaxRows];
    const size_t n = wlanWifiScanCopySnapshot(rows, kWlanWifiScanCacheMaxRows);
    for (size_t i = 0; i < n; ++i) {
        JsonObject ap = aps.add<JsonObject>();
        ap["ssid"] = rows[i].ssid;
        ap["rssi"] = rows[i].rssi;
        ap["open"] = rows[i].open;
    }
    webSendJsonDoc(req, 200, doc);
}

namespace {
bool wifiDestructivePathBlocked(AsyncWebServerRequest *req) {
    if (g_systemShutdownInProgress.load(std::memory_order_acquire) || g_factoryResetQueued.load(std::memory_order_acquire)) {
        sendErr(req, 503, "shutdown");
        return true;
    }
    if (adminApplyBlockedByOta(otaBlocksDestructiveAction())) {
        sendErr(req, 503, "busy");
        return true;
    }
    return false;
}
} // namespace

void handleApiWifiScanPost(AsyncWebServerRequest *req, JsonVariant &json) {
    if (!adminJsonRequireObject(req, json)) {
        return;
    }
    if (wifiDestructivePathBlocked(req)) {
        return;
    }
    if (wlanWifiConnectionTestOwnsRadio()) {
        sendErr(req, 503, "busy");
        return;
    }
    wlanRequestWifiScanRefresh();
    sendOk(req, 202);
}

void handleApiWifiConnectPost(AsyncWebServerRequest *req, JsonVariant &json) {
    if (!adminJsonRequireObject(req, json)) {
        return;
    }
    WlanConfig cfg{};
    const char *err = nullptr;
    bool passwordPresent = false;
    if (!parseWifiConfigFromJson(json, &cfg, &err, &passwordPresent)) {
        sendErr(req, 400, err != nullptr ? err : "ssid");
        return;
    }
    if (configIsApMode()) {
        if (wifiDestructivePathBlocked(req)) {
            return;
        }
        if (!wlanStartWifiConnectionTest(cfg)) {
            sendErr(req, 503, "test_start");
            return;
        }
        ESP_LOGI(TAG, "WiFi connect test started ssid=%s", cfg.ssid);
        sendOk(req, 200, nullptr, "/wifi-testing");
        return;
    }
    if (wifiDestructivePathBlocked(req)) {
        return;
    }
    const ScopedWebAdminApplyInFlight applyInFlight;
    if (!applyInFlight) {
        sendErr(req, 503, "shutdown");
        return;
    }
    if (!applyInFlight.commitAllowed()) {
        sendErr(req, 503, "shutdown");
        return;
    }
    WlanConfig stored{};
    const bool haveStored = wlanCopyCachedConfig(&stored);
    const bool sameSsid = haveStored && strcmp(stored.ssid, cfg.ssid) == 0;
    switch (wifiStaPasswordApply(passwordPresent, sameSsid)) {
    case WifiStaPasswordApply::KeepStored:
        wlanConfigCopyStr(cfg.pass, sizeof(cfg.pass), stored.pass);
        break;
    case WifiStaPasswordApply::UseProvided:
        break;
    case WifiStaPasswordApply::Reject:
        sendErr(req, 400, "password");
        return;
    }
    if (!wlanSaveConfigToNvs(cfg)) {
        sendErr(req, 500, "save");
        return;
    }
    ESP_LOGI(TAG, "WiFi config saved (STA) ssid=%s — rebooting", cfg.ssid);
    deferredRebootAfterWifiSave();
    sendOk(req, 200, "saved_rebooting");
}

void handleApiWifiConnectStatusGet(AsyncWebServerRequest *req) {
    const WlanWifiConnectionTestState tst = wlanGetWifiConnectionTestState();
    const char *stStr = tst == WlanWifiConnectionTestState::Idle      ? "idle"
                        : tst == WlanWifiConnectionTestState::Testing ? "testing"
                        : tst == WlanWifiConnectionTestState::Ok      ? "ok"
                                                                      : "fail";
    char ssid[kWifiSsidMaxLen]{};
    (void)wlanWifiConnectionTestSsidSnapshot(ssid, sizeof(ssid));
    JsonDocument doc;
    doc["state"] = stStr;
    doc["ssid"] = ssid;
    webSendJsonDoc(req, 200, doc);
}

void handleApiWifiConnectCommitPost(AsyncWebServerRequest *req, JsonVariant &json) {
    if (!adminJsonRequireObject(req, json)) {
        return;
    }
    if (wifiDestructivePathBlocked(req)) {
        return;
    }
    const ScopedWebAdminApplyInFlight applyInFlight;
    if (!applyInFlight) {
        sendErr(req, 503, "shutdown");
        return;
    }
    char staIp[16]{};
    if (!wlanReadStaLocalIpForCommit(staIp, sizeof(staIp))) {
        sendErr(req, 400, "not_connected");
        return;
    }
    if (!applyInFlight.commitAllowed()) {
        sendErr(req, 503, "shutdown");
        return;
    }
    if (!wlanCommitWifiConnectionTest()) {
        sendErr(req, 400, "not_ok");
        return;
    }
    deferredRebootAfterWifiSave();
    char next[32]{};
    if (!formatToBuf(std::span<char>{next}, "http://{}/", staIp)) {
        sendOk(req, 200, "committed");
        return;
    }
    sendOk(req, 200, "committed", next);
}

void handleApiWifiConnectAbortPost(AsyncWebServerRequest *req, JsonVariant &json) {
    if (!adminJsonRequireObject(req, json)) {
        return;
    }
    wlanAbortWifiConnectionTest();
    sendOk(req, 200, nullptr, "/wifi");
}

void handleApiWifiConnectRetryPost(AsyncWebServerRequest *req, JsonVariant &json) {
    if (!adminJsonRequireObject(req, json)) {
        return;
    }
    if (wlanGetWifiConnectionTestState() != WlanWifiConnectionTestState::Fail) {
        sendErr(req, 400, "not_fail");
        return;
    }
    if (!wlanRetryWifiConnectionTest()) {
        sendErr(req, 503, "test_start");
        return;
    }
    ESP_LOGI(TAG, "WiFi connect test retry started");
    sendOk(req, 200, "retrying");
}

void adminRoutesRegisterApiWifi(AsyncWebServer &ws) {
    adminOnGet(ws, "/api/wifi/config", handleApiWifiConfigGet);
    adminOnGet(ws, "/api/wifi/scan", handleApiWifiScanGet);
    adminAddJsonPost(ws, "/api/wifi/scan", handleApiWifiScanPost);
    adminAddJsonPost(ws, "/api/wifi/connect", handleApiWifiConnectPost);
    adminOnGet(ws, "/api/wifi/connect-status", handleApiWifiConnectStatusGet, ApiGuard::Ap);
    adminAddJsonPost(ws, "/api/wifi/connect-commit", handleApiWifiConnectCommitPost, ApiGuard::Ap);
    adminAddJsonPost(ws, "/api/wifi/connect-abort", handleApiWifiConnectAbortPost, ApiGuard::Ap);
    adminAddJsonPost(ws, "/api/wifi/connect-retry", handleApiWifiConnectRetryPost, ApiGuard::Ap);
}
