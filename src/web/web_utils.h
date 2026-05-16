#pragma once

#include <Arduino.h>

class AsyncWebServerRequest;
class AsyncResponseStream;

AsyncResponseStream* beginResponseStreamOr500(AsyncWebServerRequest* req, const char* mime);

void appendHtmlEscaped(Print& out, const char* s);

/** Print current HTTP CSRF token (decimal) escaped for HTML attribute/text. Requires auth initialized. */
void appendCurrentWebCsrfTokenEscaped(Print& out);

void appendJsonEscapedCStr(Print& out, const char* str);
