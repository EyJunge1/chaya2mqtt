#pragma once

#include <Arduino.h>

class AsyncWebServerRequest;
class AsyncResponseStream;

AsyncResponseStream* beginResponseStreamOr500(AsyncWebServerRequest* req, const char* mime);

void appendHtmlEscaped(Print& out, const char* s);

void appendJsonEscapedCStr(Print& out, const char* str);
