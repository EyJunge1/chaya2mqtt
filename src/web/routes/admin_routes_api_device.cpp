#include <Arduino.h>

#include "../admin_globals.h"
#include "../admin_json.h"
#include "admin_routes_api_internal.h"

#include "battery/battery.h"
#include "config/app_config.h"
#include "config/version.h"
#include "constants.h"
#include "heart/counter.h"
#include "identity/device_identity.h"
#include "mqtt/config.h"
#include "mqtt/mqtt.h"
#include "ota/ota.h"
#include "util/log_tag.h"
#include "web/csrf.h"
#include "web/web_middleware.h"
#include "web/web_utils.h"
#include "wifi/wlan.h"
#include "wifi/wlan_config.h"

#include <ESPAsyncWebServer.h>
#include <cstdio>
#include <cstring>
#include <esp_log.h>

DEFINE_LOG_TAG("WEBAPI");

namespace {

bool appendDeviceJsonObject(char* body, size_t bodyLen, size_t* pos) {
    char deviceId[kDeviceIdBufLen]{};
    buildDeviceId(deviceId, sizeof(deviceId));
    const bool ap = configIsApMode();
    char hostname[kDeviceStaHostnameBufLen]{};
    if (ap || !formatDeviceStaHostname(deviceId, hostname, sizeof(hostname))) {
        strlcpy(hostname, kDeviceHostname, sizeof(hostname));
    }

    int n = snprintf(body + *pos, bodyLen - *pos,
                     "{\"hostname\":\"%s\",\"version\":\"%s\",\"mode\":\"%s\",\"deviceId\":",
                     hostname, APP_VERSION, ap ? "ap" : "sta");
    if (n < 0 || *pos + static_cast<size_t>(n) >= bodyLen) {
        return false;
    }
    *pos += static_cast<size_t>(n);
    if (!appendJsonStringQuotedEscaped(deviceId, body, bodyLen, pos)) {
        return false;
    }
    if (ap) {
        char apSsid[kWifiSsidMaxLen]{};
        char apIp[16]{};
        (void)wlanApSetupSnapshot(apSsid, sizeof(apSsid), apIp, sizeof(apIp));
        n = snprintf(body + *pos, bodyLen - *pos, ",\"apSsid\":");
        if (n < 0 || *pos + static_cast<size_t>(n) >= bodyLen) {
            return false;
        }
        *pos += static_cast<size_t>(n);
        if (!appendJsonStringQuotedEscaped(apSsid, body, bodyLen, pos)) {
            return false;
        }
        n = snprintf(body + *pos, bodyLen - *pos, ",\"apIp\":");
        if (n < 0 || *pos + static_cast<size_t>(n) >= bodyLen) {
            return false;
        }
        *pos += static_cast<size_t>(n);
        if (!appendJsonStringQuotedEscaped(apIp, body, bodyLen, pos)) {
            return false;
        }
    }
    n = snprintf(body + *pos, bodyLen - *pos, ",\"batteryMv\":%d,\"batteryPct\":%d}",
                 batteryMilliVolts(), batteryPercent());
    if (n < 0 || *pos + static_cast<size_t>(n) >= bodyLen) {
        return false;
    }
    *pos += static_cast<size_t>(n);
    return true;
}

bool appendWifiStatusObject(char* body, size_t bodyLen, size_t* pos) {
    bool connected = false;
    char ssidBuf[kWifiSsidMaxLen]{};
    char ipStr[kIpv4StrMaxLen]{};
    char gateway[kIpv4StrMaxLen]{};
    char netmask[kIpv4StrMaxLen]{};
    char dns1[kIpv4StrMaxLen]{};
    char dns2[kIpv4StrMaxLen]{};
    int rssi = 0;
    wlanFillStaNetSnapshot(&connected, ssidBuf, sizeof(ssidBuf), ipStr, sizeof(ipStr), gateway,
                           sizeof(gateway), netmask, sizeof(netmask), dns1, sizeof(dns1), dns2,
                           sizeof(dns2), &rssi);
    if (!connected) {
        const int n = snprintf(body + *pos, bodyLen - *pos, "{\"connected\":false}");
        if (n < 0 || *pos + static_cast<size_t>(n) >= bodyLen) {
            return false;
        }
        *pos += static_cast<size_t>(n);
        return true;
    }
    int n = snprintf(body + *pos, bodyLen - *pos, "{\"connected\":true,\"ssid\":");
    if (n < 0 || *pos + static_cast<size_t>(n) >= bodyLen) {
        return false;
    }
    *pos += static_cast<size_t>(n);
    if (!appendJsonStringQuotedEscaped(ssidBuf, body, bodyLen, pos)) {
        return false;
    }
    if (!appendWifiRuntimeFields(body, bodyLen, pos, ipStr, gateway, netmask, dns1, dns2, rssi)) {
        return false;
    }
    if (*pos + 1U >= bodyLen) {
        return false;
    }
    body[(*pos)++] = '}';
    return true;
}

} // namespace

void handleApiCsrfGet(AsyncWebServerRequest* req) {
    char token[33];
    uint32_t expiresInSeconds = 0;
    webCsrfGetTokenHex(token, sizeof(token), &expiresInSeconds);
    char body[96];
    const int n = snprintf(body, sizeof(body), "{\"token\":\"%s\",\"expiresInSeconds\":%lu}",
                           token, static_cast<unsigned long>(expiresInSeconds));
    if (n < 0 || static_cast<size_t>(n) >= sizeof(body)) {
        sendErr(req, 500, "json");
        return;
    }
    webSendJson(req, 200, body);
}

void handleApiDeviceGet(AsyncWebServerRequest* req) {
    char body[512];
    size_t pos = 0;
    if (!appendDeviceJsonObject(body, sizeof(body), &pos)) {
        sendErr(req, 500, "json");
        return;
    }
    body[pos] = '\0';
    webSendJson(req, 200, body);
}

void handleApiBootstrapGet(AsyncWebServerRequest* req) {
    // PERF-07: one round-trip for cold boot (csrf + device + live status).
    char body[3072];
    size_t pos = 0;

    char token[33];
    uint32_t expiresInSeconds = 0;
    webCsrfGetTokenHex(token, sizeof(token), &expiresInSeconds);

    int n = snprintf(body, sizeof(body),
                     "{\"csrf\":{\"token\":\"%s\",\"expiresInSeconds\":%lu},\"device\":", token,
                     static_cast<unsigned long>(expiresInSeconds));
    if (n < 0 || static_cast<size_t>(n) >= sizeof(body)) {
        sendErr(req, 500, "json");
        return;
    }
    pos = static_cast<size_t>(n);
    if (!appendDeviceJsonObject(body, sizeof(body), &pos)) {
        sendErr(req, 500, "json");
        return;
    }

    n = snprintf(body + pos, sizeof(body) - pos, ",\"wifi\":");
    if (n < 0 || pos + static_cast<size_t>(n) >= sizeof(body)) {
        sendErr(req, 500, "json");
        return;
    }
    pos += static_cast<size_t>(n);
    if (!appendWifiStatusObject(body, sizeof(body), &pos)) {
        sendErr(req, 500, "json");
        return;
    }

    const bool sta = !configIsApMode();
    if (sta) {
        const int rx = heartDisplayRxDelta();
        const int tx = heartDisplayTxDelta();
        const bool configured = mqttCfgIsBrokerConfigured();
        const bool paired = mqttCfgIsPaired();
        n = snprintf(body + pos, sizeof(body) - pos,
                     ",\"chaya\":{\"rx\":%d,\"tx\":%d,\"connected\":%s,\"configured\":%s,\"paired\":%s},"
                     "\"mqtt\":{\"connected\":%s},\"update\":",
                     rx, tx, mqttIsConnected() ? "true" : "false", configured ? "true" : "false",
                     paired ? "true" : "false", mqttIsConnected() ? "true" : "false");
        if (n < 0 || pos + static_cast<size_t>(n) >= sizeof(body)) {
            sendErr(req, 500, "json");
            return;
        }
        pos += static_cast<size_t>(n);
        const size_t otaLen = otaFormatStatusJson(body + pos, sizeof(body) - pos);
        if (otaLen == 0U) {
            sendErr(req, 500, "json");
            return;
        }
        pos += otaLen;

        char lang[3]{};
        char theme[8]{};
        configCopyUiLang(lang, sizeof(lang));
        configCopyUiTheme(theme, sizeof(theme));
        n = snprintf(
            body + pos, sizeof(body) - pos,
            ",\"settings\":{\"resetDays\":%u,\"lang\":\"%s\",\"theme\":\"%s\","
            "\"ledEnabled\":%s,\"audioTxEnabled\":%s,\"audioRxEnabled\":%s,"
            "\"audioTxVolume\":%u,\"audioRxVolume\":%u,\"quietHourStart\":%u,\"quietHourEnd\":%u,"
            "\"txHz\":%u,\"txMs\":%u,\"rxHz\":%u,\"rxMs\":%u,"
            "\"nvsOk\":%s,\"applyPending\":%s}",
            static_cast<unsigned>(configGetResetPeriodDays()), lang, theme,
            configGetLedEnabled() ? "true" : "false",
            configGetAudioTxEnabled() ? "true" : "false",
            configGetAudioRxEnabled() ? "true" : "false",
            static_cast<unsigned>(configGetAudioTxVolume()),
            static_cast<unsigned>(configGetAudioRxVolume()),
            static_cast<unsigned>(configGetAudioQuietStart()),
            static_cast<unsigned>(configGetAudioQuietEnd()),
            static_cast<unsigned>(configGetAudioTxHz()), static_cast<unsigned>(configGetAudioTxMs()),
            static_cast<unsigned>(configGetAudioRxHz()), static_cast<unsigned>(configGetAudioRxMs()),
            g_webAdminSettingsNvsWriteFailed.load(std::memory_order_acquire) ? "false" : "true",
            g_webAdminSettingsApplyPending.load(std::memory_order_acquire) ? "true" : "false");
        if (n < 0 || pos + static_cast<size_t>(n) >= sizeof(body)) {
            sendErr(req, 500, "json");
            return;
        }
        pos += static_cast<size_t>(n);
    } else {
        n = snprintf(body + pos, sizeof(body) - pos,
                     ",\"chaya\":null,\"mqtt\":null,\"update\":null,\"settings\":null");
        if (n < 0 || pos + static_cast<size_t>(n) >= sizeof(body)) {
            sendErr(req, 500, "json");
            return;
        }
        pos += static_cast<size_t>(n);
    }

    if (pos + 2U > sizeof(body)) {
        sendErr(req, 500, "json");
        return;
    }
    body[pos++] = '}';
    body[pos]   = '\0';
    webSendJson(req, 200, body);
}

void adminRoutesRegisterApiDevice(AsyncWebServer& ws) {
    {
        AsyncCallbackWebHandler& h =
            ws.on("/api/csrf", HTTP_GET, [](AsyncWebServerRequest* rq) { handleApiCsrfGet(rq); });
        h.addMiddleware(mwRequireAllowedHost());
    }
    {
        AsyncCallbackWebHandler& h =
            ws.on("/api/device", HTTP_GET, [](AsyncWebServerRequest* rq) { handleApiDeviceGet(rq); });
        h.addMiddleware(mwRequireAllowedHost());
    }
    {
        AsyncCallbackWebHandler& h = ws.on(
            "/api/bootstrap", HTTP_GET, [](AsyncWebServerRequest* rq) { handleApiBootstrapGet(rq); });
        h.addMiddleware(mwRequireAllowedHost());
    }
}
