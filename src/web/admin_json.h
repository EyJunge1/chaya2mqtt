#pragma once

#include <ESPAsyncWebServer.h>

#include <cstddef>
#include <cstdio>

template<size_t N, typename Fn>
bool adminSendJsonWithBuffer(AsyncWebServerRequest* req, Fn&& build) {
    char buf[N];
    if (!build(buf, N)) {
        req->send(500);
        return false;
    }
    req->send(200, "application/json", buf);
    return true;
}
