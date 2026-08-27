#include "admin_routes.h"

#include "constants.h"
#include "web/web_utils.h"
#include "wifi/wlan.h"

#include <ESPAsyncWebServer.h>

namespace {

void redirectToSetup(AsyncWebServerRequest* req) {
    // Absolute URL helps Android/Windows captive clients leave the probe host.
    webRedirect(req, kSetupApCaptiveRedirect);
}

void sendOkEmpty(AsyncWebServerRequest* req) {
    webSendEmpty(req, 200);
}

void sendNotFound(AsyncWebServerRequest* req) {
    webSendEmpty(req, 404);
}

} // namespace

void adminRoutesRegisterCaptive(AsyncWebServer& ws) {
    // Only meaningful in SoftAP; harmless no-ops in STA (clients rarely hit these).
    auto onlyApRedirect = [](AsyncWebServerRequest* rq) {
        if (!configIsApMode()) {
            webSendEmpty(rq, 404);
            return;
        }
        redirectToSetup(rq);
    };

    ws.on("/generate_204", HTTP_GET, onlyApRedirect);       // Android
    ws.on("/gen_204", HTTP_GET, onlyApRedirect);            // Android variant
    ws.on("/hotspot-detect.html", HTTP_GET, onlyApRedirect); // Apple
    ws.on("/library/test/success.html", HTTP_GET, onlyApRedirect);
    ws.on("/canonical.html", HTTP_GET, onlyApRedirect);     // Firefox
    ws.on("/ncsi.txt", HTTP_GET, onlyApRedirect);           // Windows
    ws.on("/connecttest.txt", HTTP_GET, onlyApRedirect);    // Windows 11
    ws.on("/redirect", HTTP_GET, onlyApRedirect);           // Microsoft
    ws.on("/success.txt", HTTP_GET, [](AsyncWebServerRequest* rq) {
        if (!configIsApMode()) {
            webSendEmpty(rq, 404);
            return;
        }
        sendOkEmpty(rq);
    });
    // Repeated WPAD lookups can starve the ESP; answer 404 instead of SPA fallback.
    ws.on("/wpad.dat", HTTP_GET, [](AsyncWebServerRequest* rq) {
        if (!configIsApMode()) {
            webSendEmpty(rq, 404);
            return;
        }
        sendNotFound(rq);
    });
}
