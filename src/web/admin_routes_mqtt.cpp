#include <Arduino.h>

#include "admin_globals.h"
#include "admin_json.h"
#include "admin_routes.h"

#include "config/app_config.h"
#include "constants.h"
#include "mqtt/mqtt.h"
#include "mqtt/config.h"

#include "web_middleware.h"

#include "pages.h"
#include "web_utils.h"
#include <ESPAsyncWebServer.h>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "log_tag.h"
#include <esp_log.h>

DEFINE_LOG_TAG("WEB");

static void handleMqttStatusGet(AsyncWebServerRequest* req) {
    adminSendJsonWithBuffer<96>(req, [](char* b, size_t n) {
        const int w =
            snprintf(b, n, "{\"connected\":%s}", mqttIsConnected() ? "true" : "false");
        return w > 0 && static_cast<size_t>(w) < n;
    });
}

static void normalizePartnerIdInput(char* id, size_t idLen) {
    if (id == nullptr || idLen == 0U) {
        return;
    }
    for (size_t i = 0; i < idLen && id[i] != '\0'; ++i) {
        if (id[i] >= 'A' && id[i] <= 'F') {
            id[i] = static_cast<char>(id[i] - 'A' + 'a');
        }
    }
}

static bool partnerIdInputValid(const char* partnerId) {
    if (!deviceIdSyntaxOk(partnerId)) {
        return false;
    }
    char ownId[kDeviceIdBufLen];
    buildDeviceId(ownId, sizeof(ownId));
    return strcmp(partnerId, ownId) != 0;
}

static void handlePairingPost(AsyncWebServerRequest* req) {
    if (g_systemShutdownInProgress.load(std::memory_order_acquire)) {
        webRedirect(req, F("/"));
        return;
    }
    g_webAdminMqttNvsWriteFailed.store(false, std::memory_order_release);
    MqttConfig pending{};
    if (!mqttCfgSnapshotTimed(&pending, 2000U)) {
        req->send(503, "text/plain", "MQTT config busy");
        return;
    }

    if (!req->hasParam("partner_id", true)) {
        webRedirect(req, F("/pairing?e=partner"));
        return;
    }
    const AsyncWebParameter* p = req->getParam("partner_id", true);
    if (p == nullptr) {
        webRedirect(req, F("/pairing?e=partner"));
        return;
    }
    strlcpy(pending.partnerDeviceId, p->value().c_str(), sizeof(pending.partnerDeviceId));
    normalizePartnerIdInput(pending.partnerDeviceId, sizeof(pending.partnerDeviceId));
    if (!partnerIdInputValid(pending.partnerDeviceId)) {
        webRedirect(req, F("/pairing?e=partner"));
        return;
    }
    mqttCfgApplyPairingTopics(&pending);
    if (!mqttTopicSyntaxOk(pending.topicPub, sizeof(pending.topicPub))
        || !mqttTopicSyntaxOk(pending.topicSub, sizeof(pending.topicSub))
        || strcmp(pending.topicPub, pending.topicSub) == 0) {
        webRedirect(req, F("/pairing?e=partner"));
        return;
    }
    MqttConfig active{};
    if (!mqttCfgSnapshotTimed(&active, 2000U)) {
        req->send(503, "text/plain", "MQTT config busy");
        return;
    }
    if (mqttCfgEquals(&pending, &active)) {
        webRedirect(req, F("/pairing?saved=1"));
        return;
    }
    mqttCfgStorePending(&pending);
    g_webAdminMqttApplyVersion.fetch_add(1U, std::memory_order_acq_rel);
    webRedirect(req, F("/pairing?saved=1"));
}

static void handleMqttPost(AsyncWebServerRequest* req) {
    if (g_systemShutdownInProgress.load(std::memory_order_acquire)) {
        webRedirect(req, F("/"));
        return;
    }
    g_webAdminMqttNvsWriteFailed.store(false, std::memory_order_release);
    MqttConfig pending{};
    if (!mqttCfgSnapshotTimed(&pending, 2000U)) {
        req->send(503, "text/plain", "MQTT config busy");
        return;
    }

    if (req->hasParam("mqtt_server", true)) {
        const AsyncWebParameter* p = req->getParam("mqtt_server", true);
        if (p != nullptr) {
            strlcpy(pending.server, p->value().c_str(), sizeof(pending.server));
        }
    }
    if (req->hasParam("mqtt_port", true)) {
        const AsyncWebParameter* p = req->getParam("mqtt_port", true);
        if (p != nullptr) {
            errno            = 0;
            char* endPtr     = nullptr;
            const long v     = strtol(p->value().c_str(), &endPtr, 10);
            const bool invalid = (errno == ERANGE) || (endPtr == p->value().c_str())
                                   || (*endPtr != '\0');
            if (invalid) {
                webRedirect(req, F("/mqtt?e=port"));
                return;
            }
            pending.port = normalizeMqttPort(static_cast<int>(v));
        }
    }
    if (req->hasParam("mqtt_user", true)) {
        const AsyncWebParameter* p = req->getParam("mqtt_user", true);
        if (p != nullptr) {
            strlcpy(pending.username, p->value().c_str(), sizeof(pending.username));
        }
    }
    if (req->hasParam("mqtt_pass", true)) {
        const AsyncWebParameter* p = req->getParam("mqtt_pass", true);
        if (p != nullptr && p->value().length() > 0) {
            strlcpy(pending.password, p->value().c_str(), sizeof(pending.password));
        }
    }
    if (req->hasParam("mqtt_topic_pub", true)) {
        const AsyncWebParameter* p = req->getParam("mqtt_topic_pub", true);
        if (p != nullptr) {
            strlcpy(pending.topicPub, p->value().c_str(), sizeof(pending.topicPub));
        }
    }
    if (req->hasParam("mqtt_topic_sub", true)) {
        const AsyncWebParameter* p = req->getParam("mqtt_topic_sub", true);
        if (p != nullptr) {
            strlcpy(pending.topicSub, p->value().c_str(), sizeof(pending.topicSub));
        }
    }
    if (strcmp(pending.topicPub, pending.topicSub) == 0) {
        ESP_LOGW(TAG, "MQTT: pub and sub topics equal — resetting to defaults");
        strlcpy(pending.topicPub, kMqttDefaultTopicPub, sizeof(pending.topicPub));
        strlcpy(pending.topicSub, kMqttDefaultTopicSub, sizeof(pending.topicSub));
    }
    pending.partnerDeviceId[0] = '\0';
    if (!mqttServerSyntaxOk(pending.server, sizeof(pending.server))
        || !mqttTopicSyntaxOk(pending.topicPub, sizeof(pending.topicPub))
        || !mqttTopicSyntaxOk(pending.topicSub, sizeof(pending.topicSub))) {
        ESP_LOGW(TAG, "MQTT invalid: empty broker or bad topics");
        if (!mqttServerSyntaxOk(pending.server, sizeof(pending.server))) {
            webRedirect(req, F("/mqtt?e=broker"));
        } else {
            webRedirect(req, F("/mqtt?e=topics"));
        }
        return;
    }
    MqttConfig active{};
    if (!mqttCfgSnapshotTimed(&active, 2000U)) {
        req->send(503, "text/plain", "MQTT config busy");
        return;
    }
    if (mqttCfgEquals(&pending, &active)) {
        webRedirect(req, F("/mqtt?saved=1"));
        return;
    }
    mqttCfgStorePending(&pending);
    g_webAdminMqttApplyVersion.fetch_add(1U, std::memory_order_acq_rel);
    webRedirect(req, F("/mqtt?saved=1"));
}

void adminRoutesRegisterMqtt(AsyncWebServer& ws) {
    {
        AsyncCallbackWebHandler& h = ws.on("/mqtt", HTTP_GET, [](AsyncWebServerRequest* rq) {
            streamMqttHtmlPage(rq, rq->hasParam("saved"));
        });
        h.addMiddleware(mwRequireStaMode());
        h.addMiddleware(mwRequireSessionRedirectGet());
    }
    {
        AsyncCallbackWebHandler& h = ws.on("/mqtt", HTTP_POST, [](AsyncWebServerRequest* rq) {
            handleMqttPost(rq);
        });
        h.addMiddleware(mwRequireStaMode());
        h.addMiddleware(mwPostSessionAndCsrfRedirect("/mqtt"));
    }
    {
        AsyncCallbackWebHandler& h = ws.on("/mqtt-status", HTTP_GET, [](AsyncWebServerRequest* rq) {
            handleMqttStatusGet(rq);
        });
        h.addMiddleware(mwRequireStaMode());
        h.addMiddleware(mwRequireSessionRedirectGet());
    }
    {
        AsyncCallbackWebHandler& h = ws.on("/pairing", HTTP_GET, [](AsyncWebServerRequest* rq) {
            streamPairingPage(rq, rq->hasParam("saved"), rq->hasParam("e"));
        });
        h.addMiddleware(mwRequireStaMode());
        h.addMiddleware(mwRequireSessionRedirectGet());
    }
    {
        AsyncCallbackWebHandler& h = ws.on("/pairing", HTTP_POST, [](AsyncWebServerRequest* rq) {
            handlePairingPost(rq);
        });
        h.addMiddleware(mwRequireStaMode());
        h.addMiddleware(mwPostSessionAndCsrfRedirect("/pairing"));
    }
}
