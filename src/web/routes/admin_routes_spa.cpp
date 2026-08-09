#include <Arduino.h>

#include "admin_routes.h"

#include "web/assets/spa_css.h"
#include "web/assets/spa_html.h"
#include "web/assets/spa_js.h"
#include "web/web_middleware.h"
#include "web/web_utils.h"
#include "wifi/wlan.h"

#include <ESPAsyncWebServer.h>
#include <cstring>

namespace {

void sendGzipProgmem(AsyncWebServerRequest* req, const char* mime, const uint8_t* data,
                     size_t len, bool immutable) {
    AsyncWebServerResponse* resp = req->beginResponse(200, mime, data, len);
    resp->addHeader(F("Content-Encoding"), F("gzip"));
    webAddSecurityHeaders(resp, /*noStore=*/false);
    if (immutable) {
        resp->addHeader(F("Cache-Control"), F("public, max-age=31536000, immutable"));
    } else {
        resp->addHeader(F("Cache-Control"), F("no-cache"));
    }
    req->send(resp);
}

void sendSpaIndex(AsyncWebServerRequest* req) {
    sendGzipProgmem(req, "text/html; charset=utf-8", SPA_INDEX_HTML_GZ, SPA_INDEX_HTML_GZ_LEN,
                    false);
}

bool isSpaUiPath(const String& uri) {
    return uri == "/" || uri == "/wifi" || uri == "/wifi-testing" || uri == "/mqtt"
           || uri == "/pairing" || uri == "/settings" || uri == "/update" || uri == "/auth";
}

} // namespace

void adminRoutesRegisterSpa(AsyncWebServer& ws) {
    {
        AsyncCallbackWebHandler& h =
            ws.on("/assets/app.js", HTTP_GET, [](AsyncWebServerRequest* rq) {
                sendGzipProgmem(rq, "application/javascript; charset=utf-8", SPA_APP_JS_GZ,
                                SPA_APP_JS_GZ_LEN, true);
            });
        h.addMiddleware(mwRequireAllowedHost());
    }
    {
        AsyncCallbackWebHandler& h =
            ws.on("/assets/app.css", HTTP_GET, [](AsyncWebServerRequest* rq) {
                sendGzipProgmem(rq, "text/css; charset=utf-8", SPA_APP_CSS_GZ, SPA_APP_CSS_GZ_LEN,
                                true);
            });
        h.addMiddleware(mwRequireAllowedHost());
    }

    static const char* kSpaPaths[] = {"/",         "/wifi",     "/wifi-testing", "/mqtt",
                                      "/pairing",  "/settings", "/update",       "/auth"};
    for (const char* path : kSpaPaths) {
        AsyncCallbackWebHandler& h =
            ws.on(path, HTTP_GET, [](AsyncWebServerRequest* rq) { sendSpaIndex(rq); });
        h.addMiddleware(mwRequireAllowedHost());
    }

    ws.onNotFound([](AsyncWebServerRequest* rq) {
        if (!webRequestHostAllowed(rq)) {
            webSendEmpty(rq, 403);
            return;
        }
        const String uri = rq->url();
        if (uri.startsWith("/api/") || uri == "/events") {
            webSendEmpty(rq, 404);
            return;
        }
        if (rq->method() == HTTP_GET && (isSpaUiPath(uri) || !uri.startsWith("/assets/"))) {
            if (configIsApMode() && uri != "/wifi" && uri != "/wifi-testing" && uri != "/"
                && !uri.startsWith("/assets/")) {
                // Captive portal probes: land on Wi-Fi setup SPA.
                webRedirect(rq, F("/wifi"));
                return;
            }
            sendSpaIndex(rq);
            return;
        }
        webSendEmpty(rq, 404);
    });
}
