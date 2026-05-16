#pragma once

#include <ESPAsyncWebServer.h>

void adminRoutesRegisterWifi(AsyncWebServer& ws);

void adminRoutesRegisterMqtt(AsyncWebServer& ws);

void adminRoutesRegisterApplication(AsyncWebServer& ws);
