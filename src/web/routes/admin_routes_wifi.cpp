#include <Arduino.h>

#include "admin_routes.h"

// Legacy HTML /wifi* form routes removed; SPA uses `/api/wifi/*`.

void adminRoutesRegisterWifi(AsyncWebServer& ws) {
    (void)ws;
}
