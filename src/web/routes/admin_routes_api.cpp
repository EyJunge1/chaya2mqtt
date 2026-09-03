#include <Arduino.h>

#include "admin_routes.h"
#include "admin_routes_api_internal.h"

#include "util/log_tag.h"
#include "web/web_middleware.h"
#include "web/web_utils.h"

#include <AsyncJson.h>
#include <ESPAsyncWebServer.h>
#include <esp_log.h>

DEFINE_LOG_TAG("WEBAPI");

void sendOk(AsyncWebServerRequest *req, int code, const char *message, const char *next) {
    webSendJsonOk(req, code, message, next);
}

void sendOkQueued(AsyncWebServerRequest *req, int code, bool queued) { webSendJsonOkQueued(req, code, queued); }

void sendErr(AsyncWebServerRequest *req, int code, const char *error) {
    ESP_LOGW(TAG, "API error %d: %s", code, error != nullptr ? error : "error");
    webSendJsonError(req, code, error);
}

bool adminJsonRequireObject(AsyncWebServerRequest *req, JsonVariant &json) {
    if (json.is<JsonObject>()) {
        return true;
    }
    sendErr(req, 400, "bad_request");
    return false;
}

namespace {

void applyApiGuard(AsyncWebHandler &h, ApiGuard guard) {
    switch (guard) {
    case ApiGuard::Sta:
        h.addMiddleware(mwApiStaMode());
        break;
    case ApiGuard::Ap:
        h.addMiddleware(mwApiApMode());
        break;
    case ApiGuard::None:
        break;
    }
}

} // namespace

AsyncCallbackWebHandler &adminOnGet(AsyncWebServer &ws, const char *uri, ArRequestHandlerFunction fn, ApiGuard guard) {
    AsyncCallbackWebHandler &h = ws.on(uri, HTTP_GET, std::move(fn));
    applyApiGuard(h, guard);
    return h;
}

AsyncCallbackJsonWebHandler &adminAddJsonPost(AsyncWebServer &ws, const char *uri, ArJsonRequestHandlerFunction fn,
                                              ApiGuard guard) {
    auto *h = new AsyncCallbackJsonWebHandler(uri, std::move(fn));
    h->setMethod(HTTP_POST);
    h->setMaxContentLength(2048);
    applyApiGuard(*h, guard);
    ws.addHandler(h);
    return *h;
}

void adminRoutesRegisterApi(AsyncWebServer &ws) {
    adminRoutesRegisterApiDevice(ws);
    adminRoutesRegisterApiChaya(ws);
    adminRoutesRegisterApiWifi(ws);
    adminRoutesRegisterApiMqtt(ws);
    adminRoutesRegisterApiSettings(ws);
    adminRoutesRegisterApiSystem(ws);
    adminRoutesRegisterApiOta(ws);
}
