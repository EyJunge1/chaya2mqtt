#pragma once

#include <ESPAsyncWebServer.h>

void webEventsRegister(AsyncWebServer &ws);

void webEventsTick();
