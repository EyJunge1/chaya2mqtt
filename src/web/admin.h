#pragma once

class AsyncWebServer;

/** AsyncWebServer instance (port 80). */
AsyncWebServer& webAdminWebServer();

/** Wire wifi lifecycle to this module without wifi including web headers (QUAL-01). */
void webAdminInstallServerHooks();

/** Register all HTTP routes once (before webAdminWebServer().begin()). */
void webAdminRegisterRoutes();

/** Deferred reboot/Wi-Fi reconnect/OTA from request handlers; called from loop(). */
void webAdminLoop();

/** After Wi-Fi credentials were saved to NVS, request deferred reboot (main loop). */
void webAdminScheduleWifiConfiguredReboot();
