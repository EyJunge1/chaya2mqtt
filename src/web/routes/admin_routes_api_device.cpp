#include <Arduino.h>

#include "../admin_globals.h"
#include "admin_routes_api_internal.h"

#include "battery/battery.h"
#include "config/app_config.h"
#include "config/version.h"
#include "constants.h"
#include "identity/device_identity.h"
#include "mqtt/mqtt.h"
#include "ota/ota.h"
#include "ota/ota_json.h"
#include "web/csrf.h"
#include "web/web_middleware.h"
#include "web/web_utils.h"
#include "wifi/wlan.h"
#include "wifi/wlan_config.h"

#include <ESPAsyncWebServer.h>
#include <cstring>

void fillDeviceJson(JsonObject obj) {
    char deviceId[kDeviceIdBufLen]{};
    buildDeviceId(deviceId, sizeof(deviceId));
    const bool ap = configIsApMode();
    char hostname[kDeviceStaHostnameBufLen]{};
    if (ap || !formatDeviceStaHostname(deviceId, hostname, sizeof(hostname))) {
        strlcpy(hostname, kDeviceHostname, sizeof(hostname));
    }

    obj["hostname"] = hostname;
    obj["version"] = APP_VERSION;
    obj["mode"] = ap ? "ap" : "sta";
    obj["deviceId"] = deviceId;
    if (ap) {
        char apSsid[kWifiSsidMaxLen]{};
        char apIp[16]{};
        (void)wlanApSetupSnapshot(apSsid, sizeof(apSsid), apIp, sizeof(apIp));
        obj["apSsid"] = apSsid;
        obj["apIp"] = apIp;
    }
    fillDeviceBatteryJson(obj, batteryMilliVolts(), batteryPercent());
}

void fillDeviceBatteryJson(JsonObject obj, int mv, int pct) {
    obj["batteryMv"] = mv;
    obj["batteryPct"] = pct;
}

void fillWifiStatusJson(JsonObject obj, bool connected, const char *ssid, const char *ip, const char *gateway,
                        const char *netmask, const char *dns1, const char *dns2, int rssi) {
    obj["connected"] = connected;
    if (!connected) {
        return;
    }
    obj["ssid"] = ssid != nullptr ? ssid : "";
    fillWifiRuntimeFields(obj, ip, gateway, netmask, dns1, dns2, rssi);
}

void fillWifiStatusJson(JsonObject obj) {
    bool connected = false;
    char ssidBuf[kWifiSsidMaxLen]{};
    char ipStr[kIpv4StrMaxLen]{};
    char gateway[kIpv4StrMaxLen]{};
    char netmask[kIpv4StrMaxLen]{};
    char dns1[kIpv4StrMaxLen]{};
    char dns2[kIpv4StrMaxLen]{};
    int rssi = 0;
    wlanFillStaNetSnapshot(&connected, ssidBuf, sizeof(ssidBuf), ipStr, sizeof(ipStr), gateway, sizeof(gateway), netmask,
                           sizeof(netmask), dns1, sizeof(dns1), dns2, sizeof(dns2), &rssi);
    fillWifiStatusJson(obj, connected, ssidBuf, ipStr, gateway, netmask, dns1, dns2, rssi);
}

void handleApiCsrfGet(AsyncWebServerRequest *req) {
    char token[33];
    uint32_t expiresInSeconds = 0;
    webCsrfGetTokenHex(token, sizeof(token), &expiresInSeconds);
    JsonDocument doc;
    doc["token"] = token;
    doc["expiresInSeconds"] = expiresInSeconds;
    webSendJsonDoc(req, 200, doc);
}

void handleApiDeviceGet(AsyncWebServerRequest *req) {
    JsonDocument doc;
    fillDeviceJson(doc.to<JsonObject>());
    webSendJsonDoc(req, 200, doc);
}

void handleApiBootstrapGet(AsyncWebServerRequest *req) {
    JsonDocument doc;

    char token[33];
    uint32_t expiresInSeconds = 0;
    webCsrfGetTokenHex(token, sizeof(token), &expiresInSeconds);
    JsonObject csrf = doc["csrf"].to<JsonObject>();
    csrf["token"] = token;
    csrf["expiresInSeconds"] = expiresInSeconds;

    fillDeviceJson(doc["device"].to<JsonObject>());
    fillWifiStatusJson(doc["wifi"].to<JsonObject>());

    if (!configIsApMode()) {
        fillChayaJson(doc["chaya"].to<JsonObject>());
        fillMqttStatusJson(doc["mqtt"].to<JsonObject>(), mqttIsConnected());
        otaFillStatusJson(doc["update"].to<JsonObject>());
        fillSettingsJson(doc["settings"].to<JsonObject>());
    } else {
        doc["chaya"] = nullptr;
        doc["mqtt"] = nullptr;
        doc["update"] = nullptr;
        doc["settings"] = nullptr;
    }

    webSendJsonDoc(req, 200, doc);
}

void adminRoutesRegisterApiDevice(AsyncWebServer &ws) {
    {
        AsyncCallbackWebHandler &h = ws.on("/api/csrf", HTTP_GET, [](AsyncWebServerRequest *rq) { handleApiCsrfGet(rq); });
        h.addMiddleware(mwRequireAllowedHost());
    }
    {
        AsyncCallbackWebHandler &h = ws.on("/api/device", HTTP_GET, [](AsyncWebServerRequest *rq) { handleApiDeviceGet(rq); });
        h.addMiddleware(mwRequireAllowedHost());
    }
    {
        AsyncCallbackWebHandler &h =
            ws.on("/api/bootstrap", HTTP_GET, [](AsyncWebServerRequest *rq) { handleApiBootstrapGet(rq); });
        h.addMiddleware(mwRequireAllowedHost());
    }
}
