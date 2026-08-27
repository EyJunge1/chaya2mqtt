#pragma once

#include <ESPAsyncWebServer.h>

void adminRoutesRegisterApi(AsyncWebServer& ws);

/** OS captive-portal probes (AP mode); register before SPA onNotFound. */
void adminRoutesRegisterCaptive(AsyncWebServer& ws);

void adminRoutesRegisterSpa(AsyncWebServer& ws);
