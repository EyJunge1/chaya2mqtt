#include <Arduino.h>

#include "admin_routes.h"
#include "admin_routes_api_internal.h"

#include "util/log_tag.h"
#include "web/web_middleware.h"
#include "web/web_utils.h"

#include <ESPAsyncWebServer.h>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <esp_log.h>

DEFINE_LOG_TAG("WEBAPI");

void sendOk(AsyncWebServerRequest *req, int code, const char *extraJson) {
    if (extraJson == nullptr || extraJson[0] == '\0') {
        webSendJson(req, code, "{\"ok\":true}");
        return;
    }
    char buf[192];
    const int n = snprintf(buf, sizeof(buf), "{\"ok\":true,%s}", extraJson);
    if (n < 0 || static_cast<size_t>(n) >= sizeof(buf)) {
        webSendJson(req, code, "{\"ok\":true}");
        return;
    }
    webSendJson(req, code, buf);
}

void sendErr(AsyncWebServerRequest *req, int code, const char *error) {
    ESP_LOGW(TAG, "API error %d: %s", code, error != nullptr ? error : "error");
    char buf[128];
    const int n = snprintf(buf, sizeof(buf), "{\"ok\":false,\"error\":\"%s\"}", error != nullptr ? error : "error");
    if (n < 0 || static_cast<size_t>(n) >= sizeof(buf)) {
        webSendJson(req, code, "{\"ok\":false,\"error\":\"error\"}");
        return;
    }
    webSendJson(req, code, buf);
}

bool parseFormIntStrict(const String &text, int *out) {
    if (out == nullptr || text.length() == 0U) {
        return false;
    }
    errno = 0;
    char *end = nullptr;
    const long value = strtol(text.c_str(), &end, 10);
    if (errno == ERANGE || end == text.c_str() || *end != '\0' || value < INT_MIN || value > INT_MAX) {
        return false;
    }
    *out = static_cast<int>(value);
    return true;
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
