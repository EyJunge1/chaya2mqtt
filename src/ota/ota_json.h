#pragma once

#include "ota.h"

#include <ArduinoJson.h>

void otaFillStatusJson(JsonObject obj);
void otaFillStatusJson(JsonObject obj, const OtaStatus &st);
