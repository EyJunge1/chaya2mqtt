#include <Arduino.h>

#include "auth.h"

#include "config/app_config.h"
#include "web/web_utils.h"
#include "wifi/wlan.h"

#include <ESPAsyncWebServer.h>

static bool isPublicPath(const String& uri) {
    if (uri.startsWith("/assets/") || uri == "/favicon.ico") {
        return true;
    }
    if (uri == "/api/csrf" || uri == "/api/device" || uri == "/api/auth/login") {
        return true;
    }
    if (uri == "/events") {
        return true;
    }
    // SPA shell routes (auth enforced by JSON API + client).
    if (uri == "/" || uri == "/wifi" || uri == "/wifi-testing" || uri == "/auth") {
        return true;
    }
    if (configIsApMode()) {
        return uri.startsWith("/api/wifi");
    }
    if (uri.startsWith("/api/wifi/connect")) {
        return true;
    }
    return false;
}

bool webAuthRedirectIfUnauthenticated(AsyncWebServerRequest* req) {
    if (!configGetWebAuthEnabled() || configIsApMode()) {
        return false;
    }
    const String uri = req->url();
    if (isPublicPath(uri)) {
        return false;
    }
    if (webAuthIsAuthenticated(req)) {
        return false;
    }
    if (uri.startsWith("/api/")) {
        webSendJson(req, 401, "{\"ok\":false,\"error\":\"auth_required\"}");
        return true;
    }
    webRedirect(req, F("/auth"));
    return true;
}

void webAuthRegisterRoutes(AsyncWebServer& server) {
    (void)server;
    // Login/logout are served by `/api/auth/*` (admin_routes_api.cpp).
}
