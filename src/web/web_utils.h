#pragma once

#include <Arduino.h>

#include <cstddef>

class AsyncWebServerRequest;
class AsyncWebServerResponse;
class AsyncResponseStream;

/** Security headers. When noStore is false, omits Cache-Control (caller sets caching). */
void webAddSecurityHeaders(AsyncWebServerResponse* resp, bool noStore = true);

void webRedirect(AsyncWebServerRequest* req, const __FlashStringHelper* location);

void webRedirect(AsyncWebServerRequest* req, const char* location);

AsyncResponseStream* beginResponseStreamOr500(AsyncWebServerRequest* req, const char* mime);

void webSendJson(AsyncWebServerRequest* req, int code, const char* jsonBody);

void webSendEmpty(AsyncWebServerRequest* req, int code);

void appendHtmlEscaped(Print& out, const char* s);

void appendCurrentWebCsrfTokenEscaped(Print& out);

void appendJsonEscapedCStr(Print& out, const char* str);

bool webRequestHostAllowed(AsyncWebServerRequest* req);

/** When Origin is present, host must match the same allowlist as Host. Missing Origin is allowed. */
bool webRequestOriginAllowed(AsyncWebServerRequest* req);

bool appendJsonStringQuotedEscaped(const char* str, char* buf, size_t bufLen, size_t* inOutPos);
