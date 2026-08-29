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

void handleApiCsrfGet(AsyncWebServerRequest* req) {
    char token[33];
    uint32_t expiresInSeconds = 0;
    webCsrfGetTokenHex(token, sizeof(token), &expiresInSeconds);
    char body[96];
    const int n = snprintf(body, sizeof(body), "{\"token\":\"%s\",\"expiresInSeconds\":%lu}",
                           token, static_cast<unsigned long>(expiresInSeconds));
    if (n < 0 || static_cast<size_t>(n) >= sizeof(body)) {
        webSendEmpty(req, 500);
        return;
    }
    webSendJson(req, 200, body);
}

void handleApiDeviceGet(AsyncWebServerRequest* req) {
    char deviceId[kDeviceIdBufLen]{};
    buildDeviceId(deviceId, sizeof(deviceId));
    const bool ap = configIsApMode();
    char hostname[kDeviceStaHostnameBufLen]{};
    if (ap || !formatDeviceStaHostname(deviceId, hostname, sizeof(hostname))) {
        strlcpy(hostname, kDeviceHostname, sizeof(hostname));
    }

    char body[512];
    size_t pos = 0;
    int n = snprintf(body, sizeof(body),
                     "{\"hostname\":\"%s\",\"version\":\"%s\",\"mode\":\"%s\",\"deviceId\":",
                     hostname, APP_VERSION, ap ? "ap" : "sta");
    if (n < 0 || static_cast<size_t>(n) >= sizeof(body)) {
        webSendEmpty(req, 500);
        return;
    }
    pos = static_cast<size_t>(n);
    if (!appendJsonStringQuotedEscaped(deviceId, body, sizeof(body), &pos)) {
        webSendEmpty(req, 500);
        return;
    }
    if (ap) {
        char apSsid[kWifiSsidMaxLen]{};
        char apIp[16]{};
        (void)wlanApSetupSnapshot(apSsid, sizeof(apSsid), apIp, sizeof(apIp));
        if (pos + 1U >= sizeof(body)) {
            webSendEmpty(req, 500);
            return;
        }
        body[pos++] = ',';
        n = snprintf(body + pos, sizeof(body) - pos, "\"apSsid\":");
        if (n < 0 || pos + static_cast<size_t>(n) >= sizeof(body)) {
            webSendEmpty(req, 500);
            return;
        }
        pos += static_cast<size_t>(n);
        if (!appendJsonStringQuotedEscaped(apSsid, body, sizeof(body), &pos)) {
            webSendEmpty(req, 500);
            return;
        }
        n = snprintf(body + pos, sizeof(body) - pos, ",\"apIp\":");
        if (n < 0 || pos + static_cast<size_t>(n) >= sizeof(body)) {
            webSendEmpty(req, 500);
            return;
        }
        pos += static_cast<size_t>(n);
        if (!appendJsonStringQuotedEscaped(apIp, body, sizeof(body), &pos)) {
            webSendEmpty(req, 500);
            return;
        }
    }
    n = snprintf(body + pos, sizeof(body) - pos, ",\"batteryMv\":%d,\"batteryPct\":%d",
                 batteryMilliVolts(), batteryPercent());
    if (n < 0 || pos + static_cast<size_t>(n) >= sizeof(body)) {
        webSendEmpty(req, 500);
        return;
    }
    pos += static_cast<size_t>(n);
    if (pos + 2U > sizeof(body)) {
        webSendEmpty(req, 500);
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
}
