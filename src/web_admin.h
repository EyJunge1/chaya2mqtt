#pragma once

class AsyncWebServer;

/** AsyncWebServer-Instanz (Port 80). */
AsyncWebServer& webAdminWebServer();

/** Alle HTTP-Routen einmal registrieren (vor webAdminWebServer().begin()). */
void webAdminRegisterRoutes();

/** Deferred reboot/Wi-Fi reconnect/OTA from request handlers; called from wifiLoop(). */
void webAdminLoop();
