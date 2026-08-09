#include "web_utils.h"

#include "auth/auth.h"
#include "config/app_config.h"
#include "constants.h"
#include "wifi/wlan.h"

#include <ESPAsyncWebServer.h>
#include <cstdio>
#include <cstring>
#include <cctype>

void webAddSecurityHeaders(AsyncWebServerResponse* resp, bool noStore) {
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
    // QR codes use inline SVG (no <img>); keep img-src none.
    resp->addHeader(F("Content-Security-Policy"),
                    F("default-src 'self'; script-src 'self'; style-src 'self'; "
                      "connect-src 'self'; img-src 'none'; object-src 'none'; "
                      "base-uri 'none'; form-action 'self'; frame-ancestors 'none'"));
}

void webRedirect(AsyncWebServerRequest* req, const __FlashStringHelper* location) {
    AsyncWebServerResponse* resp = req->beginResponse(302);
    resp->addHeader(F("Location"), location);
    webAddSecurityHeaders(resp);
    req->send(resp);
}

void webRedirect(AsyncWebServerRequest* req, const char* location) {
    if (location == nullptr) {
        webRedirect(req, F("/"));
        return;
    }
    AsyncWebServerResponse* resp = req->beginResponse(302);
    resp->addHeader(F("Location"), location);
    webAddSecurityHeaders(resp);
    req->send(resp);
}

#include <esp_heap_caps.h>

static bool hostEqualsIgnoreCase(const char* host, const char* ref) {
    if (host == nullptr || ref == nullptr) {
        return false;
    }
    while (*host != '\0' && *ref != '\0') {
        if (std::tolower(static_cast<unsigned char>(*host))
            != std::tolower(static_cast<unsigned char>(*ref))) {
            return false;
        }
        ++host;
        ++ref;
    }
    return *host == '\0' && *ref == '\0';
}

static bool hostPrefixIgnoreCaseThenPortOrEnd(const char* host, const char* prefix) {
    if (host == nullptr || prefix == nullptr) {
        return false;
    }
    while (*prefix != '\0') {
        if (std::tolower(static_cast<unsigned char>(*host))
            != std::tolower(static_cast<unsigned char>(*prefix))) {
            return false;
        }
        ++host;
        ++prefix;
    }
    return *host == '\0' || *host == ':';
}

static bool webHostCStringAllowed(const char* host) {
    if (host == nullptr || host[0] == '\0') {
        return true;
    }
    if (hostEqualsIgnoreCase(host, kDeviceHostname)) {
        return true;
    }
    char localPrefix[48];
    static_cast<void>(snprintf(localPrefix, sizeof(localPrefix), "%s.local", kDeviceHostname));
    if (hostPrefixIgnoreCaseThenPortOrEnd(host, localPrefix)) {
        return true;
    }
    if (configIsApMode()) {
        // Captive portal: wildcard DNS resolves foreign probe hosts (e.g. captive.apple.com) to us.
        return true;
    }
    char ip[16]{};
    if (wlanStaConnectedOk() && wlanReadStaLocalIpForCommit(ip, sizeof(ip)) && ip[0] != '\0') {
        if (hostEqualsIgnoreCase(host, ip)) {
            return true;
        }
        const size_t ipLen = std::strlen(ip);
        if (std::strncmp(host, ip, ipLen) == 0 && host[ipLen] == ':') {
            return true;
        }
    }
    return false;
}

bool webRequestHostAllowed(AsyncWebServerRequest* req) {
    if (req == nullptr) {
        return false;
    }
    return webHostCStringAllowed(req->host().c_str());
}

bool webRequestOriginAllowed(AsyncWebServerRequest* req) {
    if (req == nullptr) {
        return false;
    }
    if (!req->hasHeader("Origin")) {
        return true;
    }
    const char* origin = req->header("Origin").c_str();
    const char* schemeEnd = std::strstr(origin, "://");
    if (schemeEnd == nullptr) {
        return false;
    }
    const char* hostStart = schemeEnd + 3;
    const char* pathStart = std::strchr(hostStart, '/');
    const size_t hostLen =
        (pathStart != nullptr) ? static_cast<size_t>(pathStart - hostStart) : std::strlen(hostStart);
    char originHost[128];
    if (hostLen >= sizeof(originHost)) {
        return false;
    }
    std::memcpy(originHost, hostStart, hostLen);
    originHost[hostLen] = '\0';
    return webHostCStringAllowed(originHost);
}

AsyncResponseStream* beginResponseStreamOr500(AsyncWebServerRequest* req, const char* mime) {
    if (esp_get_free_heap_size() < 8192U) {
        AsyncWebServerResponse* err = req->beginResponse(503, "text/plain", "Low heap");
        webAddSecurityHeaders(err);
        req->send(err);
        return nullptr;
    }
    AsyncResponseStream* resp = req->beginResponseStream(mime);
    if (resp == nullptr) {
        AsyncWebServerResponse* err = req->beginResponse(500);
        webAddSecurityHeaders(err);
        req->send(err);
        return nullptr;
    }
    webAddSecurityHeaders(resp);
    return resp;
}

void webSendJson(AsyncWebServerRequest* req, int code, const char* jsonBody) {
    const char* body = (jsonBody != nullptr) ? jsonBody : "";
    AsyncWebServerResponse* r = req->beginResponse(code, "application/json", body);
    webAddSecurityHeaders(r);
    req->send(r);
}

void webSendEmpty(AsyncWebServerRequest* req, int code) {
    AsyncWebServerResponse* r = req->beginResponse(code);
    webAddSecurityHeaders(r);
    req->send(r);
}

void appendCurrentWebCsrfTokenEscaped(Print& out) {
    char b[33];
    webAuthGetCsrfTokenHex(b, sizeof(b));
    appendHtmlEscaped(out, b);
}

void appendHtmlEscaped(Print& out, const char* s) {
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

void appendJsonEscapedCStr(Print& out, const char* str) {
    out.print('"');
    if (str == nullptr) {
        out.print('"');
        return;
    }
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(str); *p != '\0'; ++p) {
        const unsigned char c = *p;
        switch (c) {
            case '"':
                out.print(F("\\\""));
                break;
            case '\\':
                out.print(F("\\\\"));
                break;
            case '\b':
                out.print(F("\\b"));
                break;
            case '\f':
                out.print(F("\\f"));
                break;
            case '\n':
                out.print(F("\\n"));
                break;
            case '\r':
                out.print(F("\\r"));
                break;
            case '\t':
                out.print(F("\\t"));
                break;
            default:
                if (c < 0x20U) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
                    out.print(buf);
                } else {
                    out.write(static_cast<char>(c));
                }
                break;
        }
    }
    out.print('"');
}

bool appendJsonStringQuotedEscaped(const char* str, char* buf, size_t bufLen, size_t* inOutPos) {
    if (buf == nullptr || inOutPos == nullptr || *inOutPos >= bufLen) {
        return false;
    }
    size_t pos = *inOutPos;
    auto appendRaw = [&](const char* s, size_t len) -> bool {
        if (pos + len >= bufLen) {
            return false;
        }
        memcpy(buf + pos, s, len);
        pos += len;
        return true;
    };
    auto appendByte = [&](unsigned char c) -> bool {
        if (pos + 1U >= bufLen) {
            return false;
        }
        buf[pos++] = static_cast<char>(c);
        return true;
    };

    if (!appendByte('"')) {
        return false;
    }
    if (str == nullptr) {
        if (!appendByte('"')) {
            return false;
        }
        *inOutPos = pos;
        return true;
    }
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(str); *p != '\0'; ++p) {
        const unsigned char c = *p;
        switch (c) {
            case '"':
                if (!appendRaw("\\\"", 2)) {
                    return false;
                }
                break;
            case '\\':
                if (!appendRaw("\\\\", 2)) {
                    return false;
                }
                break;
            case '\b':
                if (!appendRaw("\\b", 2)) {
                    return false;
                }
                break;
            case '\f':
                if (!appendRaw("\\f", 2)) {
                    return false;
                }
                break;
            case '\n':
                if (!appendRaw("\\n", 2)) {
                    return false;
                }
                break;
            case '\r':
                if (!appendRaw("\\r", 2)) {
                    return false;
                }
                break;
            case '\t':
                if (!appendRaw("\\t", 2)) {
                    return false;
                }
                break;
            default:
                if (c < 0x20U) {
                    char ubuf[8];
                    static_cast<void>(snprintf(ubuf, sizeof(ubuf), "\\u%04x", static_cast<unsigned>(c)));
                    if (!appendRaw(ubuf, 6)) {
                        return false;
                    }
                } else {
                    if (!appendByte(c)) {
                        return false;
                    }
                }
                break;
        }
    }
    if (!appendByte('"')) {
        return false;
    }
    *inOutPos = pos;
    return true;
}
