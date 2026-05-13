#pragma once

class AsyncWebServerRequest;

void streamSimpleDonePage(AsyncWebServerRequest* req, const char* title, const char* message);
void streamDashboard(AsyncWebServerRequest* req);
void streamWifiPage(AsyncWebServerRequest* req);
void handleWifiScanJson(AsyncWebServerRequest* req);
void streamUpdatePage(AsyncWebServerRequest* req);
void streamMqttHtmlPage(AsyncWebServerRequest* req, bool showSavedBanner);
