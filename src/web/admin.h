#pragma once

class AsyncWebServer;

/** AsyncWebServer instance (port 80). */
auto webAdminWebServer() -> AsyncWebServer &;

/** Register all HTTP routes once (before webAdminWebServer().begin()). */
void webAdminRegisterRoutes();

/** Deferred reboot/Wi-Fi reconnect/OTA from request handlers; called from loop(). */
void webAdminLoop();

/** True when MQTT apply version is ahead of the last queued NetCmd. */
auto webAdminMqttApplyUnqueued() -> bool;

/** After Wi-Fi credentials were saved to NVS, request deferred reboot (main loop). */
void webAdminScheduleWifiConfiguredReboot();

/** Drop armed admin reboot / Wi-Fi-save restart (factory owns the restart). */
void webAdminClearRestartRequests();
