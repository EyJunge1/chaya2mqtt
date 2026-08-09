#include <Arduino.h>

#include "admin_routes.h"

// Legacy HTML /mqtt and /pairing form routes removed; SPA uses `/api/mqtt` and `/api/pairing`.

void adminRoutesRegisterMqtt(AsyncWebServer& ws) {
    (void)ws;
}
