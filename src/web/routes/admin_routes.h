#pragma once

#include <ESPAsyncWebServer.h>

void adminRoutesRegisterApi(AsyncWebServer& ws);

void adminRoutesRegisterSpa(AsyncWebServer& ws);
