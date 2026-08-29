#include <Arduino.h>

#include "../admin_globals.h"
#include "../admin_json.h"
#include "admin_routes_api_internal.h"

#include "async/event_types.h"
#include "async/task_handles.h"
#include "config/app_config.h"
#include "config/version.h"
#include "constants.h"
#include "identity/device_identity.h"
#include "heart/counter.h"
#include "battery/battery.h"
#include "mqtt/config.h"
#include "mqtt/mqtt.h"
#include "ota/ota.h"
#include "util/log_tag.h"
#include "web/csrf.h"
#include "web/deferred_reboot.h"
#include "web/web_middleware.h"
#include "web/web_utils.h"
#include "wifi/test.h"
#include "wifi/wlan.h"
#include "wifi/wlan_config.h"

#include <ESPAsyncWebServer.h>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <esp_log.h>

DEFINE_LOG_TAG("WEBAPI");

void handleApiMqttStatusGet(AsyncWebServerRequest* req) {
    adminSendJsonWithBuffer<96>(req, [](char* b, size_t n) {
        const int w =
            snprintf(b, n, "{\"connected\":%s}", mqttIsConnected() ? "true" : "false");
        return w > 0 && static_cast<size_t>(w) < n;
    });
}

bool fillMqttConfigJson(char* body, size_t bodyLen, const MqttConfig& cfg) {
    char deviceId[kDeviceIdBufLen]{};
    buildDeviceId(deviceId, sizeof(deviceId));

    size_t pos = 0;
    int n = snprintf(body, bodyLen, "{\"deviceId\":");
    if (n < 0 || static_cast<size_t>(n) >= bodyLen) {
        return false;
    }
    pos = static_cast<size_t>(n);
    if (!appendJsonStringQuotedEscaped(deviceId, body, bodyLen, &pos)) {
        return false;
    }
    n = snprintf(body + pos, bodyLen - pos, ",\"server\":");
    if (n < 0 || pos + static_cast<size_t>(n) >= bodyLen) {
        return false;
    }
    pos += static_cast<size_t>(n);
    if (!appendJsonStringQuotedEscaped(cfg.server, body, bodyLen, &pos)) {
        return false;
    }
    n = snprintf(body + pos, bodyLen - pos, ",\"port\":%u,\"username\":",
                 static_cast<unsigned>(cfg.port));
    if (n < 0 || pos + static_cast<size_t>(n) >= bodyLen) {
        return false;
    }
    pos += static_cast<size_t>(n);
    if (!appendJsonStringQuotedEscaped(cfg.username, body, bodyLen, &pos)) {
        return false;
    }
    n = snprintf(body + pos, bodyLen - pos, ",\"hasPassword\":%s,\"topicPub\":",
                 cfg.password[0] != '\0' ? "true" : "false");
    if (n < 0 || pos + static_cast<size_t>(n) >= bodyLen) {
        return false;
    }
    pos += static_cast<size_t>(n);
    if (!appendJsonStringQuotedEscaped(cfg.topicPub, body, bodyLen, &pos)) {
        return false;
    }
    n = snprintf(body + pos, bodyLen - pos, ",\"topicSub\":");
    if (n < 0 || pos + static_cast<size_t>(n) >= bodyLen) {
        return false;
    }
    pos += static_cast<size_t>(n);
    if (!appendJsonStringQuotedEscaped(cfg.topicSub, body, bodyLen, &pos)) {
        return false;
    }
    n = snprintf(body + pos, bodyLen - pos, ",\"partnerId\":");
    if (n < 0 || pos + static_cast<size_t>(n) >= bodyLen) {
        return false;
    }
    pos += static_cast<size_t>(n);
    if (!appendJsonStringQuotedEscaped(cfg.partnerDeviceId, body, bodyLen, &pos)) {
        return false;
    }
    if (pos + 2U > bodyLen) {
        return false;
    }
    body[pos++] = '}';
    body[pos]   = '\0';
    return true;
}

void handleApiMqttGet(AsyncWebServerRequest* req) {
    MqttConfig cfg{};
    if (!mqttCfgSnapshotTimed(&cfg, 2000U)) {
        sendErr(req, 503, "busy");
        return;
    }
    char body[768]{};
    if (!fillMqttConfigJson(body, sizeof(body), cfg)) {
        webSendEmpty(req, 500);
        return;
    }
    webSendJson(req, 200, body);
}

void normalizePartnerIdInput(char* id, size_t idLen) {
    if (id == nullptr || idLen == 0U) {
        return;
    }
    for (size_t i = 0; i < idLen && id[i] != '\0'; ++i) {
        if (id[i] >= 'A' && id[i] <= 'F') {
            id[i] = static_cast<char>(id[i] - 'A' + 'a');
        }
    }
}

bool partnerIdInputValid(const char* partnerId) {
    if (!deviceIdSyntaxOk(partnerId)) {
        return false;
    }
    char ownId[kDeviceIdBufLen];
    buildDeviceId(ownId, sizeof(ownId));
    return strcmp(partnerId, ownId) != 0;
}

void handleApiMqttPost(AsyncWebServerRequest* req) {
    if (g_systemShutdownInProgress.load(std::memory_order_acquire)) {
        sendErr(req, 503, "shutdown");
        return;
    }
    g_webAdminMqttNvsWriteFailed.store(false, std::memory_order_release);
    MqttConfig pending{};
    if (!mqttCfgSnapshotTimed(&pending, 2000U)) {
        sendErr(req, 503, "busy");
        return;
    }
    if (!adminApplyOptionalString(req, "mqtt_server", pending.server, sizeof(pending.server))) {
        sendErr(req, 400, "broker");
        return;
    }
    {
        int portIn = static_cast<int>(pending.port);
        if (!adminApplyOptionalInt(req, "mqtt_port", nullptr, &portIn)) {
            sendErr(req, 400, "port");
            return;
        }
        pending.port = normalizeMqttPort(portIn);
    }
    if (!adminApplyOptionalString(req, "mqtt_user", pending.username, sizeof(pending.username))) {
        sendErr(req, 400, "username");
        return;
    }
    {
        char passBuf[sizeof(pending.password)];
        passBuf[0] = '\0';
        if (!adminApplyOptionalString(req, "mqtt_pass", passBuf, sizeof(passBuf))) {
            sendErr(req, 400, "password");
            return;
        }
        // Empty password field means "leave unchanged" (also when absent — passBuf stays empty).
        if (req->hasParam("mqtt_pass", true) && passBuf[0] != '\0') {
            strlcpy(pending.password, passBuf, sizeof(pending.password));
        }
    }
    if (!adminApplyOptionalString(req, "partner_id", pending.partnerDeviceId,
                                  sizeof(pending.partnerDeviceId))) {
        sendErr(req, 400, "partner");
        return;
    }
    if (req->hasParam("partner_id", true)) {
        normalizePartnerIdInput(pending.partnerDeviceId, sizeof(pending.partnerDeviceId));
        if (pending.partnerDeviceId[0] != '\0' && !partnerIdInputValid(pending.partnerDeviceId)) {
            sendErr(req, 400, "partner");
            return;
        }
    }
    mqttCfgApplyPairingTopics(&pending);
    if (!mqttServerSyntaxOk(pending.server, sizeof(pending.server))) {
        sendErr(req, 400, "broker");
        return;
    }
    if (!mqttTopicSyntaxOk(pending.topicPub, sizeof(pending.topicPub))) {
        sendErr(req, 400, "topics");
        return;
    }
    if (pending.partnerDeviceId[0] != '\0') {
        if (!mqttTopicSyntaxOk(pending.topicSub, sizeof(pending.topicSub))
            || strcmp(pending.topicPub, pending.topicSub) == 0) {
            sendErr(req, 400, "partner");
            return;
        }
    } else {
        pending.topicSub[0] = '\0';
    }
    MqttConfig active{};
    if (!mqttCfgSnapshotTimed(&active, 2000U)) {
        sendErr(req, 503, "busy");
        return;
    }
    if (!mqttCfgEquals(&pending, &active)) {
        mqttCfgStorePending(&pending);
        g_webAdminMqttApplyVersion.fetch_add(1U, std::memory_order_acq_rel);
        ESP_LOGI(TAG, "MQTT settings accepted broker=%s:%u", pending.server,
                 static_cast<unsigned>(pending.port));
    } else {
        ESP_LOGI(TAG, "MQTT settings unchanged");
    }
    sendOk(req, 200, "\"message\":\"saved\"");
}


void adminRoutesRegisterApiMqtt(AsyncWebServer& ws) {
    {
        AsyncCallbackWebHandler& h = ws.on("/api/mqtt/status", HTTP_GET,
                                           [](AsyncWebServerRequest* rq) { handleApiMqttStatusGet(rq); });
        h.addMiddleware(mwRequireAllowedHost());
        h.addMiddleware(mwApiStaMode());
    }
    {
        AsyncCallbackWebHandler& h =
            ws.on("/api/mqtt", HTTP_GET, [](AsyncWebServerRequest* rq) { handleApiMqttGet(rq); });
        h.addMiddleware(mwRequireAllowedHost());
        h.addMiddleware(mwApiStaMode());
    }
    {
        AsyncCallbackWebHandler& h =
            ws.on("/api/mqtt", HTTP_POST, [](AsyncWebServerRequest* rq) { handleApiMqttPost(rq); });
        h.addMiddleware(mwApiStaMode());
        h.addMiddleware(mwApiPostCsrf());
    }
}
