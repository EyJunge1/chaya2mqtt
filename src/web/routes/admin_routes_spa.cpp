#include <Arduino.h>

#include "admin_routes.h"

#include "constants.h"
#include "web/assets/web_ui_manifest.h"
#include "web/spa_asset_lookup.h"
#include "web/web_middleware.h"
#include "web/web_utils.h"
#include "wifi/wlan.h"

#include <ESPAsyncWebServer.h>

namespace {

void sendGzipAsset(AsyncWebServerRequest* req, const SpaAssetEntry& asset) {
    const uint8_t* data = gWebUiBlobStart + asset.offset;
    AsyncWebServerResponse* resp =
        req->beginResponse(200, asset.contentType, data, asset.length);
    resp->addHeader(F("Content-Encoding"), F("gzip"));
    webAddSecurityHeaders(resp, /*noStore=*/false);
    if (asset.cache == SpaCacheClass::Immutable) {
        resp->addHeader(F("Cache-Control"), F("public, max-age=31536000, immutable"));
    } else {
        resp->addHeader(F("Cache-Control"), F("no-cache"));
    }
    req->send(resp);
}

bool sendSpaIndex(AsyncWebServerRequest* req) {
    const SpaAssetEntry* index = spaFindIndex(WEB_UI_ASSETS, WEB_UI_ASSETS_COUNT);
    if (!index) {
        webSendEmpty(req, 500);
        return false;
    }
    sendGzipAsset(req, *index);
    return true;
}

bool trySendExactAsset(AsyncWebServerRequest* req, const String& uri) {
    const SpaAssetEntry* asset = spaFindAsset(WEB_UI_ASSETS, WEB_UI_ASSETS_COUNT, uri.c_str());
    if (!asset) {
        return false;
    }
    sendGzipAsset(req, *asset);
    return true;
}

} // namespace

void adminRoutesRegisterSpa(AsyncWebServer& ws) {
    ws.onNotFound([](AsyncWebServerRequest* rq) {
        if (!webRequestHostAllowed(rq)) {
            webSendEmpty(rq, 403);
            return;
        }

        const String uri = rq->url();
        if (spaIsApiOrEventsPath(uri.c_str())) {
            webSendEmpty(rq, 404);
            return;
        }

        if (rq->method() != HTTP_GET) {
            webSendEmpty(rq, 404);
            return;
        }

        if (trySendExactAsset(rq, uri)) {
            return;
        }

        if (spaIsAssetPath(uri.c_str())) {
            webSendEmpty(rq, 404);
            return;
        }

        if (spaShouldFallbackToIndex(uri.c_str())) {
            if (configIsApMode() && spaIsCaptivePortalProbe(uri.c_str())) {
                // Dedicated captive routes should handle these; keep a safe fallback.
                webRedirect(rq, kSetupApCaptiveRedirect);
                return;
            }
            if (configIsApMode() && uri != "/wifi" && uri != "/wifi-testing" && uri != "/"
                && !uri.startsWith("/assets/")) {
                // Unknown captive / OS probes: land on Wi-Fi setup SPA.
                webRedirect(rq, F("/wifi"));
                return;
            }
            sendSpaIndex(rq);
            return;
        }

        webSendEmpty(rq, 404);
    });
}
