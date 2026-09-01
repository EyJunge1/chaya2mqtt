#include <Arduino.h>

#include "admin_routes.h"

#include "constants.h"
#include "web/assets/web_ui_manifest.h"
#include "web/spa_asset_lookup.h"
#include "web/web_middleware.h"
#include "web/web_utils.h"
#include "wifi/wlan.h"

#include <ESPAsyncWebServer.h>
#include <algorithm>
#include <cstring>

namespace {

void addSpaResponseHeaders(AsyncWebServerResponse *resp, const SpaAssetEntry &asset) {
    if (spaAssetUsesGzip(asset.path)) {
        resp->addHeader(F("Content-Encoding"), F("gzip"));
    }
    webAddSecurityHeaders(resp, /*noStore=*/false);
    if (asset.cache == SpaCacheClass::Immutable) {
        resp->addHeader(F("Cache-Control"), F("public, max-age=31536000, immutable"));
    } else {
        resp->addHeader(F("Cache-Control"), F("no-cache"));
    }
}

void sendSpaAsset(AsyncWebServerRequest *req, const SpaAssetEntry &asset) {
    const uint8_t *data = gWebUiBlobStart + asset.offset;
    const size_t len = asset.length;

    AsyncWebServerResponse *resp =
        req->beginResponse(asset.contentType, len, [data, len](uint8_t *buf, size_t maxLen, size_t index) -> size_t {
            if (index >= len) {
                return 0;
            }
            const size_t n = std::min(maxLen, len - index);
            std::memcpy(buf, data + index, n);
            return n;
        });
    addSpaResponseHeaders(resp, asset);
    req->send(resp);
}

bool sendSpaIndex(AsyncWebServerRequest *req) {
    // Compiler .rodata string — not a slice of the .incbin blob (quirks-mode / empty body).
    AsyncWebServerResponse *resp = req->beginResponse(200, "text/html; charset=utf-8", kWebUiIndexHtml);
    webAddSecurityHeaders(resp, /*noStore=*/false);
    resp->addHeader(F("Cache-Control"), F("no-cache"));
    req->send(resp);
    return true;
}

bool trySendExactAsset(AsyncWebServerRequest *req, const char *uri) {
    const SpaAssetEntry *asset = spaFindAsset(WEB_UI_ASSETS, WEB_UI_ASSETS_COUNT, uri);
    if (!asset) {
        return false;
    }
    sendSpaAsset(req, *asset);
    return true;
}

} // namespace

void adminRoutesRegisterSpa(AsyncWebServer &ws) {
    auto sendIndexIfHostOk = [](AsyncWebServerRequest *rq) {
        if (!webRequestHostAllowed(rq)) {
            webSendEmpty(rq, 403);
            return;
        }
        sendSpaIndex(rq);
    };
    ws.on("/", HTTP_GET, sendIndexIfHostOk);
    ws.on("/index.html", HTTP_GET, sendIndexIfHostOk);

    ws.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *rq) {
        if (!webRequestHostAllowed(rq)) {
            webSendEmpty(rq, 403);
            return;
        }
        webSendEmpty(rq, 204);
    });

    ws.onNotFound([](AsyncWebServerRequest *rq) {
        if (!webRequestHostAllowed(rq)) {
            webSendEmpty(rq, 403);
            return;
        }

        // PERF-09: avoid copying the URL String; AsyncWebServer keeps the buffer alive.
        const char *uri = rq->url().c_str();
        if (spaIsApiOrEventsPath(uri)) {
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

        if (spaIsAssetPath(uri)) {
            webSendEmpty(rq, 404);
            return;
        }

        if (spaShouldFallbackToIndex(uri)) {
            if (configIsApMode() && spaIsCaptivePortalProbe(uri)) {
                // Dedicated captive routes should handle these; keep a safe fallback.
                webRedirect(rq, kSetupApCaptiveRedirect);
                return;
            }
            if (configIsApMode() && strcmp(uri, "/wifi-testing") != 0 && strcmp(uri, "/") != 0 &&
                strncmp(uri, "/assets/", 8) != 0) {
                // Unknown captive / OS probes: land on AP setup SPA (root shows WifiSetup).
                webRedirect(rq, F("/"));
                return;
            }
            sendSpaIndex(rq);
            return;
        }

        webSendEmpty(rq, 404);
    });
}
