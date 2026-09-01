#include <Arduino.h>

#include "../admin_globals.h"
#include "admin_routes_api_internal.h"

#include "battery/battery.h"
#include "battery/battery_pure.h"
#include "constants.h"
#include "identity/device_identity.h"
#include "mqtt/config.h"
#include "mqtt/mqtt.h"
#include "util/log_tag.h"
#include "web/web_middleware.h"
#include "web/web_utils.h"

#include <ESPAsyncWebServer.h>
#include <cstring>
#include <esp_log.h>

DEFINE_LOG_TAG("WEBAPI");

void fillMqttStatusJson(JsonObject obj, bool connected) { obj["connected"] = connected; }

void handleApiMqttStatusGet(AsyncWebServerRequest *req) {
    JsonDocument doc;
    fillMqttStatusJson(doc.to<JsonObject>(), mqttIsConnected());
    webSendJsonDoc(req, 200, doc);
}

void fillMqttConfigJson(JsonObject obj, const MqttConfig &cfg) {
    char deviceId[kDeviceIdBufLen]{};
    buildDeviceId(deviceId, sizeof(deviceId));
    obj["deviceId"] = deviceId;
    obj["server"] = cfg.server;
    obj["port"] = cfg.port;
    obj["tls"] = cfg.tls;
    obj["username"] = cfg.username;
    obj["hasPassword"] = cfg.password[0] != '\0';
    obj["topicPub"] = cfg.topicPub;
    obj["topicSub"] = cfg.topicSub;
    obj["partnerId"] = cfg.partnerDeviceId;
    obj["nvsOk"] = !mqttCfgNvsWriteFailed();
    obj["applyPending"] = mqttCfgApplyPending();
}

void handleApiMqttGet(AsyncWebServerRequest *req) {
    MqttConfig cfg{};
    if (!mqttCfgSnapshotTimed(&cfg, 2000U)) {
        sendErr(req, 503, "busy");
        return;
    }
    JsonDocument doc;
    fillMqttConfigJson(doc.to<JsonObject>(), cfg);
    webSendJsonDoc(req, 200, doc);
}

void normalizePartnerIdInput(char *id, size_t idLen) {
    if (id == nullptr || idLen == 0U) {
        return;
    }
    for (size_t i = 0; i < idLen && id[i] != '\0'; ++i) {
        if (id[i] >= 'A' && id[i] <= 'F') {
            id[i] = static_cast<char>(id[i] - 'A' + 'a');
        }
    }
}

bool partnerIdInputValid(const char *partnerId) {
    if (!deviceIdSyntaxOk(partnerId)) {
        return false;
    }
    char ownId[kDeviceIdBufLen];
    buildDeviceId(ownId, sizeof(ownId));
    return strcmp(partnerId, ownId) != 0;
}

void handleApiMqttPost(AsyncWebServerRequest *req, JsonVariant &json) {
    if (!adminJsonRequireObject(req, json)) {
        return;
    }
    if (g_systemShutdownInProgress.load(std::memory_order_acquire)) {
        sendErr(req, 503, "shutdown");
        return;
    }
    if (batteryCriticalLow(batteryPercent())) {
        sendErr(req, 503, "battery_low");
        return;
    }
    mqttCfgSetNvsWriteFailed(false);
    MqttConfig pending{};
    if (!mqttCfgSnapshotTimed(&pending, 2000U)) {
        sendErr(req, 503, "busy");
        return;
    }
    if (!adminApplyOptionalString(json, "mqtt_server", pending.server, sizeof(pending.server))) {
        sendErr(req, 400, "broker");
        return;
    }
    {
        int portIn = static_cast<int>(pending.port);
        if (!adminApplyOptionalInt(json, "mqtt_port", nullptr, &portIn)) {
            sendErr(req, 400, "port");
            return;
        }
        pending.port = normalizeMqttPort(portIn);
    }
    if (!adminApplyOptionalBool(json, "mqtt_tls", &pending.tls)) {
        sendErr(req, 400, "tls");
        return;
    }
    if (!adminApplyOptionalString(json, "mqtt_user", pending.username, sizeof(pending.username))) {
        sendErr(req, 400, "username");
        return;
    }
    if (!mqttUsernameSyntaxOk(pending.username, sizeof(pending.username))) {
        sendErr(req, 400, "username");
        return;
    }
    {
        char passBuf[sizeof(pending.password)];
        passBuf[0] = '\0';
        if (!adminApplyOptionalString(json, "mqtt_pass", passBuf, sizeof(passBuf))) {
            sendErr(req, 400, "password");
            return;
        }
        // Empty password field means "leave unchanged" (also when absent — passBuf stays empty).
        if (adminJsonHasField(json, "mqtt_pass") && passBuf[0] != '\0') {
            if (!mqttPasswordSyntaxOk(passBuf, sizeof(passBuf))) {
                sendErr(req, 400, "password");
                return;
            }
            strlcpy(pending.password, passBuf, sizeof(pending.password));
        }
    }
    if (!adminApplyOptionalString(json, "partner_id", pending.partnerDeviceId, sizeof(pending.partnerDeviceId))) {
        sendErr(req, 400, "partner");
        return;
    }
    if (adminJsonHasField(json, "partner_id")) {
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
        if (!mqttTopicSyntaxOk(pending.topicSub, sizeof(pending.topicSub)) || strcmp(pending.topicPub, pending.topicSub) == 0) {
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
        mqttCfgSetApplyPending(true);
        g_webAdminMqttApplyVersion.fetch_add(1U, std::memory_order_acq_rel);
        ESP_LOGI(TAG, "MQTT settings accepted broker=%s:%u tls=%s", pending.server, static_cast<unsigned>(pending.port),
                 pending.tls ? "yes" : "no");
    } else {
        ESP_LOGI(TAG, "MQTT settings unchanged");
    }
    sendOk(req, 200, "saved");
}

void adminRoutesRegisterApiMqtt(AsyncWebServer &ws) {
    {
        AsyncCallbackWebHandler &h =
            ws.on("/api/mqtt/status", HTTP_GET, [](AsyncWebServerRequest *rq) { handleApiMqttStatusGet(rq); });
        h.addMiddleware(mwRequireAllowedHost());
        h.addMiddleware(mwApiStaMode());
    }
    {
        AsyncCallbackWebHandler &h = ws.on("/api/mqtt", HTTP_GET, [](AsyncWebServerRequest *rq) { handleApiMqttGet(rq); });
        h.addMiddleware(mwRequireAllowedHost());
        h.addMiddleware(mwApiStaMode());
    }
    {
        AsyncCallbackJsonWebHandler &h = adminAddJsonPost(ws, "/api/mqtt", handleApiMqttPost);
        h.addMiddleware(mwApiStaMode());
        h.addMiddleware(mwRequireAllowedHost());
    }
}
