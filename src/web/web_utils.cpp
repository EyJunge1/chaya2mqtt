#include "web_utils.h"

#include "config/app_config.h"
#include "constants.h"
#include "csrf.h"
#include "host_validate.h"
#include "identity/device_identity.h"
#include "wifi/wlan.h"

#include <AsyncJson.h>
#include <ESPAsyncWebServer.h>
#include <cstdio>
#include <cstring>

void webAddSecurityHeaders(AsyncWebServerResponse *resp, bool noStore) {
    if (resp == nullptr) {
        return;
    }
    resp->addHeader(F("X-Frame-Options"), F("DENY"));
    resp->addHeader(F("X-Content-Type-Options"), F("nosniff"));
    resp->addHeader(F("X-XSS-Protection"), F("0"));
    resp->addHeader(F("Referrer-Policy"), F("no-referrer"));
    if (noStore) {
        resp->addHeader(F("Cache-Control"), F("no-store"));
    }
    resp->addHeader(F("Permissions-Policy"), F("camera=(), microphone=(), geolocation=()"));
    // SPA assets are external same-origin files (no inline JS/CSS required).
    // Favicon uses data: / same-origin; QR codes are inline SVG.
    // Captive browsers inject small helper scripts; allow only their exact hashes
    // instead of weakening the policy with unsafe-inline.
    resp->addHeader(F("Content-Security-Policy"), F("default-src 'self'; "
                                                    "script-src 'self' "
                                                    "'sha256-/wvEleeS5MQ7yyfJEOR4qpa8+JUfhOSb/nL6ABAsJ8s=' "
                                                    "'sha256-S7aCyzLyXJK5s4JHzbN5gslKO2OjyLe1PK4tB4Bnu4U='; "
                                                    "style-src 'self'; "
                                                    "connect-src 'self'; img-src 'self' data:; object-src 'none'; "
                                                    "base-uri 'none'; form-action 'self'; frame-ancestors 'none'"));
}

void webRedirect(AsyncWebServerRequest *req, const __FlashStringHelper *location) {
    AsyncWebServerResponse *resp = req->beginResponse(302);
    resp->addHeader(F("Location"), location);
    webAddSecurityHeaders(resp);
    req->send(resp);
}

void webRedirect(AsyncWebServerRequest *req, const char *location) {
    if (location == nullptr) {
        webRedirect(req, F("/"));
        return;
    }
    AsyncWebServerResponse *resp = req->beginResponse(302);
    resp->addHeader(F("Location"), location);
    webAddSecurityHeaders(resp);
    req->send(resp);
}

#include <esp_heap_caps.h>

static bool webHostAllowedForRequest(const char *host) {
    const bool apMode = configIsApMode();
    char ip[16]{};
    const bool hasIp = !apMode && wlanStaConnectedOk() && wlanReadStaLocalIpForCommit(ip, sizeof(ip)) && ip[0] != '\0';
    char hostname[kDeviceStaHostnameBufLen]{};
    if (apMode || !buildDeviceStaHostname(hostname, sizeof(hostname))) {
        strlcpy(hostname, kDeviceHostname, sizeof(hostname));
    }
    return webHostCStringAllowed(host, apMode, hostname, hasIp ? ip : nullptr);
}

bool webRequestHostAllowed(AsyncWebServerRequest *req) {
    if (req == nullptr) {
        return false;
    }
    return webHostAllowedForRequest(req->host().c_str());
}

bool webRequestOriginAllowed(AsyncWebServerRequest *req) {
    if (req == nullptr) {
        return false;
    }
    if (!req->hasHeader("Origin")) {
        return true;
    }
    const char *origin = req->header("Origin").c_str();
    const char *schemeEnd = std::strstr(origin, "://");
    if (schemeEnd == nullptr) {
        return false;
    }
    const char *hostStart = schemeEnd + 3;
    const char *pathStart = std::strchr(hostStart, '/');
    const size_t hostLen = (pathStart != nullptr) ? static_cast<size_t>(pathStart - hostStart) : std::strlen(hostStart);
    char originHost[128];
    if (hostLen >= sizeof(originHost)) {
        return false;
    }
    std::memcpy(originHost, hostStart, hostLen);
    originHost[hostLen] = '\0';
    return webHostAllowedForRequest(originHost);
}

void webSendJsonDoc(AsyncWebServerRequest *req, int code, JsonDocument &doc) {
    if (doc.overflowed()) {
        webSendEmpty(req, 500);
        return;
    }
    AsyncJsonResponse *resp = new AsyncJsonResponse();
    resp->setCode(code);
    JsonObject dest = resp->getRoot().to<JsonObject>();
    dest.set(doc.as<JsonObjectConst>());
    if (resp->overflowed()) {
        delete resp;
        webSendEmpty(req, 500);
        return;
    }
    resp->setLength();
    webAddSecurityHeaders(resp);
    req->send(resp);
}

void webSendJsonError(AsyncWebServerRequest *req, int code, const char *error) {
    JsonDocument doc;
    doc["ok"] = false;
    doc["error"] = error != nullptr ? error : "error";
    webSendJsonDoc(req, code, doc);
}

void webSendJsonOk(AsyncWebServerRequest *req, int code, const char *message, const char *next) {
    JsonDocument doc;
    doc["ok"] = true;
    if (message != nullptr && message[0] != '\0') {
        doc["message"] = message;
    }
    if (next != nullptr && next[0] != '\0') {
        doc["next"] = next;
    }
    webSendJsonDoc(req, code, doc);
}

void webSendJsonOkQueued(AsyncWebServerRequest *req, int code, bool queued) {
    JsonDocument doc;
    doc["ok"] = true;
    doc["queued"] = queued;
    webSendJsonDoc(req, code, doc);
}

size_t webSerializeJson(const JsonDocument &doc, char *buf, size_t bufLen) {
    if (buf == nullptr || bufLen == 0U || doc.overflowed()) {
        return 0;
    }
    const size_t n = serializeJson(doc, buf, bufLen);
    if (n == 0U || n >= bufLen) {
        return 0;
    }
    return n;
}

void webSendEmpty(AsyncWebServerRequest *req, int code) {
    AsyncWebServerResponse *r = req->beginResponse(code);
    webAddSecurityHeaders(r);
    req->send(r);
}

void appendCurrentWebCsrfTokenEscaped(Print &out) {
    char b[33];
    webCsrfGetTokenHex(b, sizeof(b));
    appendHtmlEscaped(out, b);
}

void appendHtmlEscaped(Print &out, const char *s) {
    if (s == nullptr) {
        return;
    }
    for (; *s != '\0'; ++s) {
        switch (*s) {
        case '&':
            out.print(F("&amp;"));
            break;
        case '"':
            out.print(F("&quot;"));
            break;
        case '\'':
            out.print(F("&#39;"));
            break;
        case '<':
            out.print(F("&lt;"));
            break;
        case '>':
            out.print(F("&gt;"));
            break;
        default:
            out.print(*s);
            break;
        }
    }
}
