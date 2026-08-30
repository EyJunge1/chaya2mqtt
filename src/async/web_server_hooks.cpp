#include "web_server_hooks.h"

namespace {
WebServerVoidFn s_registerRoutes      = nullptr;
WebServerVoidFn s_begin               = nullptr;
WebServerVoidFn s_end                 = nullptr;
WebServerVoidFn s_afterWifiSaveReboot = nullptr;
} // namespace

void webServerHooksRegister(WebServerVoidFn registerRoutes, WebServerVoidFn begin, WebServerVoidFn end,
                            WebServerVoidFn afterWifiSaveReboot) {
    s_registerRoutes      = registerRoutes;
    s_begin               = begin;
    s_end                 = end;
    s_afterWifiSaveReboot = afterWifiSaveReboot;
}

void webServerRegisterRoutes() {
    if (s_registerRoutes != nullptr) {
        s_registerRoutes();
    }
}

void webServerBegin() {
    if (s_begin != nullptr) {
        s_begin();
    }
}

void webServerEnd() {
    if (s_end != nullptr) {
        s_end();
    }
}

void webRequestRebootAfterWifiSave() {
    if (s_afterWifiSaveReboot != nullptr) {
        s_afterWifiSaveReboot();
    }
}
