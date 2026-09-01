#pragma once

#include <ESPAsyncWebServer.h>

#include "web_utils.h"

#include <cstddef>
#include <cstdio>

void sendErr(AsyncWebServerRequest *req, int code, const char *error);

template <size_t N, typename Fn> bool adminSendJsonWithBuffer(AsyncWebServerRequest *req, Fn &&build) {
    char buf[N];
    if (!build(buf, N)) {
        sendErr(req, 500, "json");
        return false;
    }
    webSendJson(req, 200, buf);
    return true;
}
