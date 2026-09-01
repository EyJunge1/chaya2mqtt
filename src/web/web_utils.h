#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include <cstddef>

class AsyncWebServerRequest;
class AsyncWebServerResponse;

/** Security headers. When noStore is false, omits Cache-Control (caller sets caching). */
void webAddSecurityHeaders(AsyncWebServerResponse *resp, bool noStore = true);

void webRedirect(AsyncWebServerRequest *req, const __FlashStringHelper *location);

void webRedirect(AsyncWebServerRequest *req, const char *location);

void webSendJsonDoc(AsyncWebServerRequest *req, int code, JsonDocument &doc);

void webSendJsonError(AsyncWebServerRequest *req, int code, const char *error);

void webSendJsonOk(AsyncWebServerRequest *req, int code, const char *message = nullptr, const char *next = nullptr);

void webSendJsonOkQueued(AsyncWebServerRequest *req, int code, bool queued);

/** Serialize doc into buf. Returns bytes written (excl. NUL) or 0 on document or buffer overflow. */
size_t webSerializeJson(const JsonDocument &doc, char *buf, size_t bufLen);

void webSendEmpty(AsyncWebServerRequest *req, int code);

void appendHtmlEscaped(Print &out, const char *s);

void appendCurrentWebCsrfTokenEscaped(Print &out);

bool webRequestHostAllowed(AsyncWebServerRequest *req);

/** When Origin is present, host must match the same allowlist as Host. Missing Origin is allowed. */
bool webRequestOriginAllowed(AsyncWebServerRequest *req);
