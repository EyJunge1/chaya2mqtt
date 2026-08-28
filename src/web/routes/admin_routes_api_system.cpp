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

void handleApiRebootPost(AsyncWebServerRequest* req) {
    if (g_systemShutdownInProgress.load(std::memory_order_acquire)) {
        sendErr(req, 503, "shutdown");
        return;
    }
    ESP_LOGI(TAG, "API reboot requested");
    g_webAdminRebootRequested.store(true, std::memory_order_release);
    sendOk(req, 200, "\"message\":\"rebooting\"");
}

void handleApiResetPost(AsyncWebServerRequest* req, NetCmd cmd, const char* message) {
    if (g_systemShutdownInProgress.load(std::memory_order_acquire)) {
        sendErr(req, 503, "shutdown");
        return;
    }
    if (otaBlocksDestructiveAction()) {
        sendErr(req, 503, "busy");
        return;
    }
    if (g_netCmdQueue == nullptr) {
        sendErr(req, 503, "unavailable");
        return;
    }
    if (xQueueSend(g_netCmdQueue, &cmd, 0) != pdTRUE) {
        sendErr(req, 503, "queue_full");
        return;
    }
    ESP_LOGW(TAG, "API reset queued: %s", message != nullptr ? message : "?");
    char extra[64];
    const int n = snprintf(extra, sizeof(extra), "\"message\":\"%s\"", message);
    sendOk(req, 202, n > 0 && static_cast<size_t>(n) < sizeof(extra) ? extra : nullptr);
}


void adminRoutesRegisterApiSystem(AsyncWebServer& ws) {
    {
        AsyncCallbackWebHandler& h =
            ws.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest* rq) { handleApiRebootPost(rq); });
        h.addMiddleware(mwApiStaMode());
        h.addMiddleware(mwApiPostCsrf());
    }
    {
        AsyncCallbackWebHandler& h =
            ws.on("/api/factory-reset", HTTP_POST,
                  [](AsyncWebServerRequest* rq) {
                      handleApiResetPost(rq, NetCmd::FactoryResetRequested, "factory_reset");
                  });
        h.addMiddleware(mwApiStaMode());
        h.addMiddleware(mwApiPostCsrf());
    }
}
