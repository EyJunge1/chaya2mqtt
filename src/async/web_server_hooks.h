#pragma once

/**
 * Indirection so non-web modules may start/stop the HTTP server and request post-save reboot
 * without #include "web/…". Implementations are registered once from main/web admin (QUAL-01).
 * Boot: main calls RegisterRoutes → setupWiFi → Begin. Teardown: callers End before reset.
 */
using WebServerVoidFn = void (*)();

void webServerHooksRegister(WebServerVoidFn registerRoutes, WebServerVoidFn begin, WebServerVoidFn end,
                            WebServerVoidFn afterWifiSaveReboot);

void webServerRegisterRoutes();
void webServerBegin();
void webServerEnd();

/** Ask the registered admin hook to defer reboot after Wi‑Fi NVS commit. */
void webRequestRebootAfterWifiSave();
