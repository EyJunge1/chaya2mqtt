#include <Arduino.h>

#include "admin_globals.h"
#include "admin_routes.h"

#include "app_config.h"
#include "constants.h"
#include "mqtt.h"
#include "mqtt_config.h"
#include "wlan.h"

#include "auth.h"
#include "pages.h"

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
    if (configIsApMode()) {
        req->redirect(F("/"));
        return;
    }
    if (webAuthRedirectIfUnauthenticated(req)) {
        return;
    }
    AsyncResponseStream* resp = req->beginResponseStream("application/json");
    if (resp == nullptr) {
        req->send(500);
        return;
    }
    resp->print(mqttIsConnected() ? F("{\"connected\":true}") : F("{\"connected\":false}"));
    req->send(resp);
}

static void handleMqttPost(AsyncWebServerRequest* req) {
    if (!webAuthIsAuthenticated(req)) {
        req->redirect(F("/auth"));
        return;
    }
    if (!webAuthValidateCsrfPost(req)) {
        req->redirect(F("/mqtt"));
        return;
    }

    MqttConfig pending{};
    mqttCfgSnapshot(&pending);

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
                req->redirect(F("/mqtt"));
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
    if (!mqttServerSyntaxOk(pending.server, sizeof(pending.server))
        || !mqttTopicSyntaxOk(pending.topicPub, sizeof(pending.topicPub))
        || !mqttTopicSyntaxOk(pending.topicSub, sizeof(pending.topicSub))) {
        ESP_LOGW(TAG, "MQTT invalid: empty broker or bad topics");
        req->redirect(F("/mqtt"));
        return;
    }
    mqttCfgStorePending(&pending);
    g_webAdminMqttApplyPending.store(true, std::memory_order_release);
    req->redirect(F("/mqtt?saved=1"));
}

void adminRoutesRegisterMqtt(AsyncWebServer& ws) {
    ws.on("/mqtt", HTTP_GET, [](AsyncWebServerRequest* rq) {
        if (configIsApMode()) {
            rq->redirect(F("/"));
            return;
        }
        if (webAuthRedirectIfUnauthenticated(rq)) {
            return;
        }
        streamMqttHtmlPage(rq, rq->hasParam("saved"));
    });
    ws.on("/mqtt", HTTP_POST, [](AsyncWebServerRequest* rq) {
        if (configIsApMode()) {
            rq->redirect(F("/"));
            return;
        }
        handleMqttPost(rq);
    });
    ws.on("/mqtt-status", HTTP_GET,
        [](AsyncWebServerRequest* rq) {
            handleMqttStatusGet(rq);
        });
}
