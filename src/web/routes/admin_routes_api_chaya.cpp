#include <Arduino.h>

#include "admin_routes_api_internal.h"

#include "heart/counter.h"
#include "mqtt/config.h"
#include "mqtt/mqtt.h"
#include "web/web_middleware.h"
#include "web/web_utils.h"

#include <ESPAsyncWebServer.h>

void fillChayaJson(JsonObject obj, int rx, int tx, bool connected, bool configured, bool paired) {
    obj["rx"] = rx;
    obj["tx"] = tx;
    obj["connected"] = connected;
    obj["configured"] = configured;
    obj["paired"] = paired;
}

void fillChayaJson(JsonObject obj) {
    fillChayaJson(obj, heartDisplayRxDelta(), heartDisplayTxDelta(), mqttIsConnected(), mqttCfgIsBrokerConfigured(),
                  mqttCfgIsPaired());
}

void handleApiChayaGet(AsyncWebServerRequest *req) {
    JsonDocument doc;
    fillChayaJson(doc.to<JsonObject>());
    webSendJsonDoc(req, 200, doc);
}

void handleApiChayaSendPost(AsyncWebServerRequest *req, JsonVariant &json) {
    if (!adminJsonRequireObject(req, json)) {
        return;
    }
    switch (chayaRequestSend()) {
    case ChayaSendResult::Started:
        sendOkQueued(req, 202, true);
        return;
    case ChayaSendResult::Busy:
        sendErr(req, 503, "busy");
        return;
    case ChayaSendResult::Unavailable:
        sendErr(req, 503, "unavailable");
        return;
    }
    sendErr(req, 503, "unavailable");
}

void adminRoutesRegisterApiChaya(AsyncWebServer &ws) {
    {
        AsyncCallbackWebHandler &h = ws.on("/api/chaya", HTTP_GET, [](AsyncWebServerRequest *rq) { handleApiChayaGet(rq); });
        h.addMiddleware(mwRequireAllowedHost());
        h.addMiddleware(mwApiStaMode());
    }
    {
        AsyncCallbackJsonWebHandler &h = adminAddJsonPost(ws, "/api/chaya/send", handleApiChayaSendPost);
        h.addMiddleware(mwApiStaMode());
        h.addMiddleware(mwApiPostCsrf());
    }
}
