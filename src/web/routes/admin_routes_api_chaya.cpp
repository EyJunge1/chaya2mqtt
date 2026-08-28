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

void handleApiChayaGet(AsyncWebServerRequest* req) {
    const int  rx         = heartDisplayRxDelta();
    const int  tx         = heartDisplayTxDelta();
    const bool configured = mqttCfgIsBrokerConfigured();
    adminSendJsonWithBuffer<160>(req, [rx, tx, configured](char* b, size_t n) {
        const int w =
            snprintf(b, n, "{\"rx\":%d,\"tx\":%d,\"connected\":%s,\"configured\":%s}", rx, tx,
                     mqttIsConnected() ? "true" : "false", configured ? "true" : "false");
        return w > 0 && static_cast<size_t>(w) < n;
    });
}

void handleApiChayaSendPost(AsyncWebServerRequest* req) {
    if (g_systemShutdownInProgress.load(std::memory_order_acquire) || !mqttIsConnected()
        || mqttPublishBlocked() || g_netCmdQueue == nullptr) {
        sendErr(req, 503, "unavailable");
        return;
    }
    const NetCmd cmd = NetCmd::ChayaSendRequested;
    if (xQueueSend(g_netCmdQueue, &cmd, 0) != pdTRUE) {
        sendErr(req, 503, "queue_full");
        return;
    }
    sendOk(req, 202, "\"queued\":true");
}


void adminRoutesRegisterApiChaya(AsyncWebServer& ws) {
    {
        AsyncCallbackWebHandler& h =
            ws.on("/api/chaya", HTTP_GET, [](AsyncWebServerRequest* rq) { handleApiChayaGet(rq); });
        h.addMiddleware(mwRequireAllowedHost());
        h.addMiddleware(mwApiStaMode());
    }
    {
        AsyncCallbackWebHandler& h = ws.on("/api/chaya/send", HTTP_POST,
                                           [](AsyncWebServerRequest* rq) { handleApiChayaSendPost(rq); });
        h.addMiddleware(mwApiStaMode());
        h.addMiddleware(mwApiPostCsrf());
    }
}
