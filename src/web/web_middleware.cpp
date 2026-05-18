#include "web_middleware.h"

#include "auth.h"
#include "config/app_config.h"
#include "wifi/wlan.h"

ArMiddlewareCallback mwRequireStaMode() {
    return [](AsyncWebServerRequest* req, ArMiddlewareNext next) {
        if (configIsApMode()) {
            req->redirect(F("/"));
            return;
        }
        next();
    };
}

ArMiddlewareCallback mwRequireApMode() {
    return [](AsyncWebServerRequest* req, ArMiddlewareNext next) {
        if (!configIsApMode()) {
            req->redirect(F("/"));
            return;
        }
        next();
    };
}

ArMiddlewareCallback mwRequireSessionRedirectGet() {
    return [](AsyncWebServerRequest* req, ArMiddlewareNext next) {
        if (webAuthRedirectIfUnauthenticated(req)) {
            return;
        }
        next();
    };
}

ArMiddlewareCallback mwPostSessionAndCsrfRedirect(const char* csrfRedirectPath) {
    return [csrfRedirectPath](AsyncWebServerRequest* req, ArMiddlewareNext next) {
        if (!webAuthIsAuthenticated(req)) {
            req->redirect(F("/auth"));
            return;
        }
        if (!webAuthValidateCsrfPost(req)) {
            req->redirect(csrfRedirectPath);
            return;
        }
        next();
    };
}

ArMiddlewareCallback mwPostChayaSendGuard() {
    return [](AsyncWebServerRequest* req, ArMiddlewareNext next) {
        if (!webAuthIsAuthenticated(req)) {
            req->send(401, "application/json", "{\"ok\":false}");
            return;
        }
        if (!webAuthValidateCsrfPost(req)) {
            req->send(403, "application/json", "{\"ok\":false}");
            return;
        }
        next();
    };
}

ArMiddlewareCallback mwApPostCsrfRedirect(const char* redirectOnMismatch) {
    return [redirectOnMismatch](AsyncWebServerRequest* req, ArMiddlewareNext next) {
        if (!webAuthValidateCsrfPost(req)) {
            req->redirect(redirectOnMismatch);
            return;
        }
        next();
    };
}

ArMiddlewareCallback mwWifiConnectPostGuard() {
    return [](AsyncWebServerRequest* req, ArMiddlewareNext next) {
        if (configIsApMode()) {
            if (!webAuthValidateCsrfPost(req)) {
                req->redirect(F("/wifi"));
                return;
            }
            next();
            return;
        }
        // STA: hidden CSRF only when web auth is enabled.
        if (!configGetWebAuthEnabled()) {
            next();
            return;
        }
        if (!webAuthIsAuthenticated(req)) {
            req->redirect(F("/auth"));
            return;
        }
        if (!webAuthValidateCsrfPost(req)) {
            req->redirect(F("/wifi"));
            return;
        }
        next();
    };
}

ArMiddlewareCallback mwWifiInfoOrApOpenGet() {
    return [](AsyncWebServerRequest* req, ArMiddlewareNext next) {
        if (configIsApMode()) {
            next();
            return;
        }
        if (webAuthRedirectIfUnauthenticated(req)) {
            return;
        }
        next();
    };
}
