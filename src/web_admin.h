#pragma once

class AsyncWebServer;

/** AsyncWebServer-Instanz (Port 80). */
AsyncWebServer& webAdminWebServer();

/** Alle HTTP-Routen einmal registrieren (vor webAdminWebServer().begin()). */
void webAdminRegisterRoutes();

/** Deferred Reboot/WiFi-OTA aus Request-Handlers; wird aus configLoop() aufgerufen. */
void webAdminLoop();
