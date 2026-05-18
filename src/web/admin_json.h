#pragma once

#include <ESPAsyncWebServer.h>

#include "web_utils.h"

#include <cstddef>
#include <cstdio>

template<size_t N, typename Fn>
bool adminSendJsonWithBuffer(AsyncWebServerRequest* req, Fn&& build) {
    char buf[N];
    if (!build(buf, N)) {
        webSendEmpty(req, 500);
        return false;
    }
    webSendJson(req, 200, buf);
    return true;
}
