#include <Arduino.h>

#include "admin_globals.h"
#include "admin_routes.h"

#include "config/app_config.h"
#include "constants.h"
#include "heart/counter.h"
#include "mqtt/mqtt.h"
#include "wifi/wlan.h"

#include "auth.h"
#include "display/display.h"
#include "mqtt/config.h"
#include "ota/ota.h"
#include "pages.h"
#include "web_utils.h"

#include <ESPAsyncWebServer.h>
#include <algorithm>
#include <climits>

static void handleChayaStatusGet(AsyncWebServerRequest* req) {
    if (configIsApMode()) {
        req->redirect(F("/"));
        return;
    }
    if (webAuthRedirectIfUnauthenticated(req)) {
        return;
    }
    AsyncResponseStream* resp = req->beginResponseStream("application/json");
    if (resp == nullptr) {
        req->send(500);
        return;
    }
    const int rx = std::max(0, heartCounter - counterBaseline);
    const int tx = std::max(0, heartSentCounter - sentCountBaseline);
    resp->print(F("{\"rx\":"));
    resp->print(rx);
    resp->print(F(",\"tx\":"));
    resp->print(tx);
    resp->print(F(",\"connected\":"));
    resp->print(mqttIsConnected() ? F("true}") : F("false}"));
    req->send(resp);
}

static void handleChayaSendPost(AsyncWebServerRequest* req) {
    if (configIsApMode()) {
        req->redirect(F("/"));
        return;
    }
    if (!webAuthIsAuthenticated(req)) {
        req->send(401, "application/json", "{\"ok\":false}");
        return;
    }
    if (!webAuthValidateCsrfPost(req)) {
        req->send(403, "application/json", "{\"ok\":false}");
        return;
    }
    if (!mqttIsConnected()) {
        req->send(200, "application/json", "{\"ok\":false}");
        return;
    }
    g_webAdminChayaSendRequested.store(true, std::memory_order_release);
    req->send(200, "application/json", "{\"ok\":true}");
}

static void handleUpdateCheckPost(AsyncWebServerRequest* req) {
    if (!webAuthIsAuthenticated(req)) {
        req->redirect(F("/auth"));
        return;
    }
    if (!webAuthValidateCsrfPost(req)) {
        req->redirect(F("/update"));
        return;
    }
    otaQueueGithubCheck();
    streamSimpleDonePage(req, "Update",
        "Checking GitHub for updates — the device may install shortly afterward.");
}

static void handleRebootPost(AsyncWebServerRequest* req) {
    if (!webAuthIsAuthenticated(req)) {
        req->redirect(F("/auth"));
        return;
    }
    if (!webAuthValidateCsrfPost(req)) {
        req->redirect(F("/"));
        return;
    }
    g_webAdminRebootRequested.store(true, std::memory_order_release);
    streamSimpleDonePage(req, "Reboot", "Rebooting…");
}

static void handleSettingsPost(AsyncWebServerRequest* req) {
    if (!webAuthIsAuthenticated(req)) {
        req->redirect(F("/auth"));
        return;
    }
    if (!webAuthValidateCsrfPost(req)) {
        req->redirect(F("/settings"));
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
    g_webAdminPendingResetDays  = days;
    g_webAdminPendingAuthEnabled = req->hasParam("auth_enabled", true);
    portEXIT_CRITICAL(&g_webAdminSettingsPendingMux);
    g_webAdminSettingsApplyPending.store(true, std::memory_order_release);
    req->redirect(F("/settings?saved=1"));
}

void adminRoutesRegisterApplication(AsyncWebServer& ws) {
    ws.on("/", HTTP_GET, [](AsyncWebServerRequest* rq) {
        if (webAuthRedirectIfUnauthenticated(rq)) {
            return;
        }
        streamDashboard(rq);
    });
    ws.on("/update", HTTP_GET, [](AsyncWebServerRequest* rq) {
        if (configIsApMode()) {
            rq->redirect(F("/"));
            return;
        }
        if (webAuthRedirectIfUnauthenticated(rq)) {
            return;
        }
        streamUpdatePage(rq);
    });
    ws.on("/update-check", HTTP_POST, [](AsyncWebServerRequest* rq) {
        if (configIsApMode()) {
            rq->redirect(F("/"));
            return;
        }
        handleUpdateCheckPost(rq);
    });
    ws.on("/reboot", HTTP_POST, [](AsyncWebServerRequest* rq) {
        if (configIsApMode()) {
            rq->redirect(F("/"));
            return;
        }
        handleRebootPost(rq);
    });

    ws.on("/chaya-status", HTTP_GET, [](AsyncWebServerRequest* rq) {
        handleChayaStatusGet(rq);
    });
    ws.on("/chaya-send", HTTP_POST, [](AsyncWebServerRequest* rq) {
        if (configIsApMode()) {
            rq->redirect(F("/"));
            return;
        }
        handleChayaSendPost(rq);
    });

    ws.on("/settings", HTTP_GET, [](AsyncWebServerRequest* rq) {
        if (configIsApMode()) {
            rq->redirect(F("/"));
            return;
        }
        if (webAuthRedirectIfUnauthenticated(rq)) {
            return;
        }
        streamSettingsPage(rq, rq->hasParam("saved"));
    });
    ws.on("/settings", HTTP_POST, [](AsyncWebServerRequest* rq) {
        if (configIsApMode()) {
            rq->redirect(F("/"));
            return;
        }
        handleSettingsPost(rq);
    });

    ws.onNotFound([](AsyncWebServerRequest* rq) {
        rq->redirect(F("/"));
    });
}
