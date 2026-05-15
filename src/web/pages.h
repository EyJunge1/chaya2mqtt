#pragma once

class AsyncWebServerRequest;

void streamAuthPage(AsyncWebServerRequest* req, bool wrongCode);
void streamSimpleDonePage(AsyncWebServerRequest* req, const char* title, const char* message);
void streamDashboard(AsyncWebServerRequest* req);
void streamWifiPage(AsyncWebServerRequest* req);
void streamWifiTestingPage(AsyncWebServerRequest* req);
void handleWifiScanJson(AsyncWebServerRequest* req);
void streamUpdatePage(AsyncWebServerRequest* req);
void streamMqttHtmlPage(AsyncWebServerRequest* req, bool showSavedBanner);
void streamSettingsPage(AsyncWebServerRequest* req, bool showSavedBanner);
