#include <Arduino.h>

#include "../admin_globals.h"
#include "admin_routes_api_internal.h"

#include "battery/battery.h"
#include "config/app_config.h"
#include "heart/counter.h"
#include "config/version.h"
#include "constants.h"
#include "identity/device_identity.h"
#include "mqtt/config.h"
#include "mqtt/mqtt.h"
#include "ota/ota.h"
#include "ota/ota_json.h"
#include "web/events.h"
#include "web/sse_send_pure.h"
#include "web/web_utils.h"
#include "web/wifi_status_cache_pure.h"
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
    obj["version"] = kAppVersion;
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
    WifiStaNetFields live{};
    if (!webWifiStaNetSnapshotOrCached(&live)) {
        fillWifiStatusJson(obj, false, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0);
        return;
    }
    fillWifiStatusJson(obj, live.connected, live.ssid, live.ip, live.gateway, live.netmask, live.dns1, live.dns2, live.rssi);
}

void handleApiBootstrapGet(AsyncWebServerRequest *req) {
    JsonDocument doc;

    fillDeviceJson(doc["device"].to<JsonObject>());
    fillWifiStatusJson(doc["wifi"].to<JsonObject>());

    if (!configIsApMode()) {
        const bool mqttConn = mqttIsConnected();
        const bool mqttConfigured = mqttCfgIsBrokerConfigured();
        int chayaRx = 0;
        int chayaTx = 0;
        heartCounterFillChayaDeltas(&chayaRx, &chayaTx);
        fillChayaJson(doc["chaya"].to<JsonObject>(), chayaRx, chayaTx, mqttConn, mqttConfigured, mqttCfgIsPaired());
        fillMqttStatusJson(doc["mqtt"].to<JsonObject>(), mqttPageConn(mqttConfigured, mqttConn));
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

void adminRoutesRegisterApiDevice(AsyncWebServer &ws) { adminOnGet(ws, "/api/bootstrap", handleApiBootstrapGet); }
