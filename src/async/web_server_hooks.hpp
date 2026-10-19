#pragma once

/**
 * HTTP server lifecycle for modules that must not #include "web/…".
 * Implemented in web/admin.cpp — missing link is a linker error, not a silent no-op.
 * Boot: main calls RegisterRoutes → setupWiFi → Begin. Teardown: End before reset.
 */
void webServerRegisterRoutes();
void webServerBegin();
void webServerEnd();

/** Drop armed admin reboot / Wi-Fi-save restart (factory owns the restart). */
void webAdminClearRestartRequests();
