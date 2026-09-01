#include "admin_routes.h"

#include "constants.h"
#include "web/web_utils.h"
#include "wifi/wlan.h"

#include <ESPAsyncWebServer.h>

namespace {

void redirectToSetup(AsyncWebServerRequest *req) {
    // Absolute URL helps Android/Windows captive clients leave the probe host.
    webRedirect(req, kSetupApCaptiveRedirect);
}

void sendOkEmpty(AsyncWebServerRequest *req) { webSendEmpty(req, 200); }

void sendNotFound(AsyncWebServerRequest *req) { webSendEmpty(req, 404); }

} // namespace

void adminRoutesRegisterCaptive(AsyncWebServer &ws) {
    // Only meaningful in SoftAP; harmless no-ops in STA (clients rarely hit these).
    // OS probes use foreign Host headers — skip the server Host allowlist.
    auto addProbe = [&](const char *uri, ArRequestHandlerFunction fn) {
        AsyncCallbackWebHandler &h = ws.on(uri, HTTP_GET, std::move(fn));
        h.skipServerMiddlewares();
    };

    auto onlyApRedirect = [](AsyncWebServerRequest *rq) {
        if (!configIsApMode()) {
            webSendEmpty(rq, 404);
            return;
        }
        redirectToSetup(rq);
    };

    addProbe("/generate_204", onlyApRedirect);        // Android
    addProbe("/gen_204", onlyApRedirect);             // Android variant
    addProbe("/hotspot-detect.html", onlyApRedirect); // Apple
    addProbe("/library/test/success.html", onlyApRedirect);
    addProbe("/canonical.html", onlyApRedirect);  // Firefox
    addProbe("/ncsi.txt", onlyApRedirect);        // Windows
    addProbe("/connecttest.txt", onlyApRedirect); // Windows 11
    addProbe("/redirect", onlyApRedirect);        // Microsoft
    addProbe("/success.txt", [](AsyncWebServerRequest *rq) {
        if (!configIsApMode()) {
            webSendEmpty(rq, 404);
            return;
        }
        sendOkEmpty(rq);
    });
    // Repeated WPAD lookups can starve the ESP; answer 404 instead of SPA fallback.
    addProbe("/wpad.dat", [](AsyncWebServerRequest *rq) {
        if (!configIsApMode()) {
            webSendEmpty(rq, 404);
            return;
        }
        sendNotFound(rq);
    });
}
