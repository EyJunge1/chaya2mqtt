#pragma once

#include "ota.hpp"

#include <ArduinoJson.h>

void otaFillStatusJson(JsonObject obj);
void otaFillStatusJson(JsonObject obj, const OtaStatus &st);
