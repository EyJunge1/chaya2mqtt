#pragma once

#include <Arduino.h>

#include <cstddef>

class AsyncWebServerRequest;
class AsyncResponseStream;

AsyncResponseStream* beginResponseStreamOr500(AsyncWebServerRequest* req, const char* mime);

void appendHtmlEscaped(Print& out, const char* s);

void appendCurrentWebCsrfTokenEscaped(Print& out);

void appendJsonEscapedCStr(Print& out, const char* str);

bool appendJsonStringQuotedEscaped(const char* str, char* buf, size_t bufLen, size_t* inOutPos);
