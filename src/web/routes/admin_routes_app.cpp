#include <Arduino.h>

#include "../admin_globals.h"
#include "../admin_json.h"
#include "admin_routes.h"

#include "async/event_types.h"
#include "async/task_handles.h"

#include "config/app_config.h"
#include "constants.h"
#include "heart/counter.h"
#include "mqtt/mqtt.h"
#include "wifi/wlan.h"

#include "ota/ota.h"
#include "../pages/pages.h"
#include "../web_middleware.h"
#include "../web_utils.h"

#include <ESPAsyncWebServer.h>
#include <esp_log.h>

#include "util/log_tag.h"

DEFINE_LOG_TAG("WEB");

static void handleChayaStatusGet(AsyncWebServerRequest* req) {
    const int rx = heartDisplayRxDelta();
    const int tx = heartDisplayTxDelta();
    adminSendJsonWithBuffer<144>(req, [rx, tx](char* b, size_t n) {
        const int w = snprintf(b, n, "{\"rx\":%d,\"tx\":%d,\"connected\":%s}", rx, tx,
                               mqttIsConnected() ? "true" : "false");
        return w > 0 && static_cast<size_t>(w) < n;
    });
}

static void handleChayaSendPost(AsyncWebServerRequest* req) {
    if (g_systemShutdownInProgress.load(std::memory_order_acquire)) {
        webSendJson(req, 503, "{\"ok\":false}");
        return;
    }
    if (!mqttIsConnected()) {
        webSendJson(req, 503, "{\"ok\":false}");
        return;
    }
    if (mqttPublishBlocked()) {
        webSendJson(req, 503, "{\"ok\":false}");
        return;
    }
    if (g_netCmdQueue == nullptr) {
        webSendJson(req, 503, "{\"ok\":false}");
        return;
    }
    const NetCmd cmd = NetCmd::ChayaSendRequested;
    if (xQueueSend(g_netCmdQueue, &cmd, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Web UI: chaya-send queue full");
        webSendJson(req, 503, "{\"ok\":false}");
        return;
    }
    ESP_LOGI(TAG, "Web UI: chaya-send POST queued");
    webSendJson(req, 202, "{\"ok\":true,\"queued\":true}");
}

static void handleUpdateCheckPost(AsyncWebServerRequest* req) {
    ESP_LOGI(TAG, "Web UI: update-check POST (GitHub)");
    otaQueueGithubCheck();
    streamSimpleDonePage(req, "Update",
        "Checking GitHub for updates — the device may install shortly afterward.");
}

static void handleRebootPost(AsyncWebServerRequest* req) {
    if (g_systemShutdownInProgress.load(std::memory_order_acquire)) {
        webRedirect(req, F("/"));
        return;
    }
    ESP_LOGI(TAG, "Web UI: reboot POST");
    g_webAdminRebootRequested.store(true, std::memory_order_release);
    streamSimpleDonePage(req, "Reboot", "Rebooting…");
}

static void handleSettingsPost(AsyncWebServerRequest* req) {
    if (g_systemShutdownInProgress.load(std::memory_order_acquire)) {
        webRedirect(req, F("/"));
        return;
    }
    uint8_t days = 7;
    if (req->hasParam("reset_days", true)) {
        const AsyncWebParameter* p = req->getParam("reset_days", true);
        if (p != nullptr) {
            const int v = p->value().toInt();
            days = (v >= 0 && v <= 30) ? static_cast<uint8_t>(v) : 7;
        }
    }
    portENTER_CRITICAL(&g_webAdminSettingsPendingMux);
    g_webAdminPendingResetDays   = days;
    g_webAdminPendingAuthEnabled = req->hasParam("auth_enabled", true);
    portEXIT_CRITICAL(&g_webAdminSettingsPendingMux);
    g_webAdminSettingsApplyPending.store(true, std::memory_order_release);
    webRedirect(req, F("/settings?saved=1"));
}

void adminRoutesRegisterApplication(AsyncWebServer& ws) {
    {
        AsyncCallbackWebHandler& h = ws.on("/", HTTP_GET, [](AsyncWebServerRequest* rq) {
            streamDashboard(rq);
        });
        h.addMiddleware(mwRequireSessionRedirectGet());
    }
    {
        AsyncCallbackWebHandler& h = ws.on("/update", HTTP_GET, [](AsyncWebServerRequest* rq) {
            streamUpdatePage(rq);
        });
        h.addMiddleware(mwRequireStaMode());
        h.addMiddleware(mwRequireSessionRedirectGet());
    }
    {
        AsyncCallbackWebHandler& h = ws.on("/update-check", HTTP_POST, [](AsyncWebServerRequest* rq) {
            handleUpdateCheckPost(rq);
        });
        h.addMiddleware(mwRequireStaMode());
        h.addMiddleware(mwPostSessionAndCsrfRedirect("/update"));
    }
    {
        AsyncCallbackWebHandler& h = ws.on("/reboot", HTTP_POST, [](AsyncWebServerRequest* rq) {
            handleRebootPost(rq);
        });
        h.addMiddleware(mwRequireStaMode());
        h.addMiddleware(mwPostSessionAndCsrfRedirect("/"));
    }

    {
        AsyncCallbackWebHandler& h = ws.on("/chaya-status", HTTP_GET, [](AsyncWebServerRequest* rq) {
            handleChayaStatusGet(rq);
        });
        h.addMiddleware(mwRequireStaMode());
        h.addMiddleware(mwRequireSessionRedirectGet());
    }
    {
        AsyncCallbackWebHandler& h = ws.on("/chaya-send", HTTP_POST, [](AsyncWebServerRequest* rq) {
            handleChayaSendPost(rq);
        });
        h.addMiddleware(mwRequireStaMode());
        h.addMiddleware(mwPostChayaSendGuard());
    }

    {
        AsyncCallbackWebHandler& h = ws.on("/settings", HTTP_GET, [](AsyncWebServerRequest* rq) {
            streamSettingsPage(rq, rq->hasParam("saved"));
        });
        h.addMiddleware(mwRequireStaMode());
        h.addMiddleware(mwRequireSessionRedirectGet());
    }
    {
        AsyncCallbackWebHandler& h = ws.on("/settings", HTTP_POST, [](AsyncWebServerRequest* rq) {
            handleSettingsPost(rq);
        });
        h.addMiddleware(mwRequireStaMode());
        h.addMiddleware(mwPostSessionAndCsrfRedirect("/settings"));
    }

    ws.onNotFound([](AsyncWebServerRequest* rq) { webRedirect(rq, F("/")); });
}
