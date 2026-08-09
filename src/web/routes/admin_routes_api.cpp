#include <Arduino.h>

#include "../admin_globals.h"
#include "../admin_json.h"
#include "admin_routes.h"

#include "async/event_types.h"
#include "async/task_handles.h"
#include "config/app_config.h"
#include "config/version.h"
#include "constants.h"
#include "heart/counter.h"
#include "mqtt/config.h"
#include "mqtt/mqtt.h"
#include "ota/ota.h"
#include "util/auth_code_validation.h"
#include "util/log_tag.h"
#include "util/time_helpers.h"
#include "hw/button.h"
#include "web/auth/auth.h"
#include "web/auth/auth_config.h"
#include "web/auth/auth_internal.h"
#include "web/deferred_reboot.h"
#include "web/web_middleware.h"
#include "web/web_utils.h"
#include "wifi/test.h"
#include "wifi/wlan.h"
#include "wifi/wlan_config.h"

#include <ESPAsyncWebServer.h>
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <esp_log.h>
#include <esp_random.h>

DEFINE_LOG_TAG("WEBAPI");

namespace {

void sendOk(AsyncWebServerRequest* req, int code = 200, const char* extraJson = nullptr) {
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

void sendErr(AsyncWebServerRequest* req, int code, const char* error) {
    char buf[128];
    const int n = snprintf(buf, sizeof(buf), "{\"ok\":false,\"error\":\"%s\"}",
                           error != nullptr ? error : "error");
    if (n < 0 || static_cast<size_t>(n) >= sizeof(buf)) {
        webSendJson(req, code, "{\"ok\":false,\"error\":\"error\"}");
        return;
    }
    webSendJson(req, code, buf);
}

void handleApiCsrfGet(AsyncWebServerRequest* req) {
    char token[33];
    webAuthGetCsrfTokenHex(token, sizeof(token));
    char body[64];
    const int n = snprintf(body, sizeof(body), "{\"token\":\"%s\"}", token);
    if (n < 0 || static_cast<size_t>(n) >= sizeof(body)) {
        webSendEmpty(req, 500);
        return;
    }
    webSendJson(req, 200, body);
}

void handleApiDeviceGet(AsyncWebServerRequest* req) {
    char deviceId[kDeviceIdBufLen]{};
    buildDeviceId(deviceId, sizeof(deviceId));
    const bool ap          = configIsApMode();
    const bool authEnabled = configGetWebAuthEnabled();
    const bool authed      = webAuthIsAuthenticated(req);
    const bool authRequired = !ap && authEnabled && !authed;
    if (authRequired) {
        maybeStartAwaitingButtonConfirm(/*resetFailStreak=*/false);
    }

    char body[320];
    size_t pos = 0;
    int n = snprintf(body, sizeof(body),
                     "{\"hostname\":\"%s\",\"version\":\"%s\",\"mode\":\"%s\",\"deviceId\":",
                     kDeviceHostname, APP_VERSION, ap ? "ap" : "sta");
    if (n < 0 || static_cast<size_t>(n) >= sizeof(body)) {
        webSendEmpty(req, 500);
        return;
    }
    pos = static_cast<size_t>(n);
    if (!appendJsonStringQuotedEscaped(deviceId, body, sizeof(body), &pos)) {
        webSendEmpty(req, 500);
        return;
    }
    n = snprintf(body + pos, sizeof(body) - pos,
                 ",\"authEnabled\":%s,\"authRequired\":%s,\"authenticated\":%s}",
                 authEnabled ? "true" : "false", authRequired ? "true" : "false",
                 authRequired ? "false" : "true");
    if (n < 0 || pos + static_cast<size_t>(n) >= sizeof(body)) {
        webSendEmpty(req, 500);
        return;
    }
    webSendJson(req, 200, body);
}

void handleApiChayaGet(AsyncWebServerRequest* req) {
    const int rx = heartDisplayRxDelta();
    const int tx = heartDisplayTxDelta();
    adminSendJsonWithBuffer<144>(req, [rx, tx](char* b, size_t n) {
        const int w = snprintf(b, n, "{\"rx\":%d,\"tx\":%d,\"connected\":%s}", rx, tx,
                               mqttIsConnected() ? "true" : "false");
        return w > 0 && static_cast<size_t>(w) < n;
    });
}

void handleApiChayaSendPost(AsyncWebServerRequest* req) {
    if (g_systemShutdownInProgress.load(std::memory_order_acquire) || !mqttIsConnected()
        || mqttPublishBlocked() || g_netCmdQueue == nullptr) {
        sendErr(req, 503, "unavailable");
        return;
    }
    const NetCmd cmd = NetCmd::ChayaSendRequested;
    if (xQueueSend(g_netCmdQueue, &cmd, 0) != pdTRUE) {
        sendErr(req, 503, "queue_full");
        return;
    }
    sendOk(req, 202, "\"queued\":true");
}

void handleApiWifiStatusGet(AsyncWebServerRequest* req) {
    bool connected = false;
    char ipStr[16]{};
    char ssidBuf[kWifiSsidMaxLen]{};
    int  rssi = 0;
    wlanFillStaLinkSnapshot(&connected, ipStr, sizeof(ipStr), ssidBuf, sizeof(ssidBuf), &rssi);
    if (!connected) {
        webSendJson(req, 200, "{\"connected\":false}");
        return;
    }
    char   body[384]{};
    size_t pos = 0;
    const int h = snprintf(body, sizeof(body),
                           "{\"connected\":true,\"ip\":\"%s\",\"rssi\":%d,\"ssid\":", ipStr, rssi);
    if (h < 0 || static_cast<size_t>(h) >= sizeof(body)) {
        webSendEmpty(req, 500);
        return;
    }
    pos = static_cast<size_t>(h);
    if (!appendJsonStringQuotedEscaped(ssidBuf, body, sizeof(body), &pos)) {
        webSendEmpty(req, 500);
        return;
    }
    if (pos + 2U > sizeof(body)) {
        webSendEmpty(req, 500);
        return;
    }
    body[pos++] = '}';
    body[pos]   = '\0';
    webSendJson(req, 200, body);
}

void handleApiWifiScanGet(AsyncWebServerRequest* req) {
    if (!wlanWifiScanCacheReady()) {
        webSendEmpty(req, 202);
        return;
    }
    AsyncResponseStream* resp = beginResponseStreamOr500(req, "application/json");
    if (resp == nullptr) {
        return;
    }
    const size_t n = wlanWifiScanCachedCount();
    resp->print('[');
    for (size_t i = 0; i < n; ++i) {
        WlanScanRow row{};
        if (!wlanWifiScanCopyRowAt(i, &row)) {
            break;
        }
        if (i > 0U) {
            resp->print(',');
        }
        resp->print(F("{\"ssid\":"));
        appendJsonEscapedCStr(*resp, row.ssid);
        resp->print(F(",\"rssi\":"));
        resp->print(row.rssi);
        resp->print(row.open ? F(",\"open\":true}") : F(",\"open\":false}"));
    }
    resp->print(']');
    req->send(resp);
    wlanRequestWifiScanRefresh();
}

void handleApiWifiConnectPost(AsyncWebServerRequest* req) {
    char ssid[kWifiSsidMaxLen];
    char password[kWifiPassMaxLen];
    ssid[0]     = '\0';
    password[0] = '\0';
    if (!adminParseBodyParam(req, "ssid", ssid, sizeof(ssid)) || ssid[0] == '\0'
        || !wifiSsidSyntaxOk(ssid, sizeof(ssid))) {
        sendErr(req, 400, "ssid");
        return;
    }
    (void)adminParseBodyParam(req, "password", password, sizeof(password));
    if (configIsApMode()) {
        if (!wlanStartWifiConnectionTest(ssid, password)) {
            sendErr(req, 503, "test_start");
            return;
        }
        sendOk(req, 200, "\"next\":\"/wifi-testing\"");
        return;
    }
    if (!configSaveWiFiCredentials(ssid, password)) {
        sendErr(req, 500, "save");
        return;
    }
    deferredRebootAfterWifiSave();
    sendOk(req, 200, "\"message\":\"saved_rebooting\"");
}

void handleApiWifiConnectStatusGet(AsyncWebServerRequest* req) {
    const WlanWifiConnectionTestState tst = wlanGetWifiConnectionTestState();
    const char* stStr = tst == WlanWifiConnectionTestState::Idle      ? "idle"
                        : tst == WlanWifiConnectionTestState::Testing ? "testing"
                        : tst == WlanWifiConnectionTestState::Ok      ? "ok"
                                                                      : "fail";
    char ssid[kWifiSsidMaxLen]{};
    (void)wlanWifiConnectionTestSsidSnapshot(ssid, sizeof(ssid));
    char   body[256]{};
    size_t pos = 0;
    const int h = snprintf(body, sizeof(body), "{\"state\":\"%s\",\"ssid\":", stStr);
    if (h < 0 || static_cast<size_t>(h) >= sizeof(body)) {
        webSendEmpty(req, 500);
        return;
    }
    pos = static_cast<size_t>(h);
    if (!appendJsonStringQuotedEscaped(ssid, body, sizeof(body), &pos)) {
        webSendEmpty(req, 500);
        return;
    }
    if (pos + 2U > sizeof(body)) {
        webSendEmpty(req, 500);
        return;
    }
    body[pos++] = '}';
    body[pos]   = '\0';
    webSendJson(req, 200, body);
}

void handleApiWifiConnectCommitPost(AsyncWebServerRequest* req) {
    char staIp[16]{};
    (void)wlanReadStaLocalIpForCommit(staIp, sizeof(staIp));
    if (!wlanCommitWifiConnectionTestAndScheduleReboot()) {
        sendErr(req, 400, "not_ok");
        return;
    }
    sendOk(req, 200, "\"message\":\"committed\"");
}

void handleApiWifiConnectAbortPost(AsyncWebServerRequest* req) {
    wlanAbortWifiConnectionTest();
    sendOk(req, 200, "\"next\":\"/wifi\"");
}

void handleApiMqttStatusGet(AsyncWebServerRequest* req) {
    adminSendJsonWithBuffer<96>(req, [](char* b, size_t n) {
        const int w =
            snprintf(b, n, "{\"connected\":%s}", mqttIsConnected() ? "true" : "false");
        return w > 0 && static_cast<size_t>(w) < n;
    });
}

bool fillMqttConfigJson(char* body, size_t bodyLen, const MqttConfig& cfg) {
    size_t pos = 0;
    int n = snprintf(body, bodyLen, "{\"server\":");
    if (n < 0 || static_cast<size_t>(n) >= bodyLen) {
        return false;
    }
    pos = static_cast<size_t>(n);
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
    if (req->hasParam("mqtt_server", true)) {
        const AsyncWebParameter* p = req->getParam("mqtt_server", true);
        if (p != nullptr) {
            strlcpy(pending.server, p->value().c_str(), sizeof(pending.server));
        }
    }
    if (req->hasParam("mqtt_port", true)) {
        const AsyncWebParameter* p = req->getParam("mqtt_port", true);
        if (p != nullptr) {
            errno        = 0;
            char* endPtr = nullptr;
            const long v = strtol(p->value().c_str(), &endPtr, 10);
            if ((errno == ERANGE) || (endPtr == p->value().c_str()) || (*endPtr != '\0')) {
                sendErr(req, 400, "port");
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
        strlcpy(pending.topicPub, kMqttDefaultTopicPub, sizeof(pending.topicPub));
        strlcpy(pending.topicSub, kMqttDefaultTopicSub, sizeof(pending.topicSub));
    }
    pending.partnerDeviceId[0] = '\0';
    if (!mqttServerSyntaxOk(pending.server, sizeof(pending.server))) {
        sendErr(req, 400, "broker");
        return;
    }
    if (!mqttTopicSyntaxOk(pending.topicPub, sizeof(pending.topicPub))
        || !mqttTopicSyntaxOk(pending.topicSub, sizeof(pending.topicSub))) {
        sendErr(req, 400, "topics");
        return;
    }
    MqttConfig active{};
    if (!mqttCfgSnapshotTimed(&active, 2000U)) {
        sendErr(req, 503, "busy");
        return;
    }
    if (!mqttCfgEquals(&pending, &active)) {
        mqttCfgStorePending(&pending);
        g_webAdminMqttApplyVersion.fetch_add(1U, std::memory_order_acq_rel);
    }
    sendOk(req, 200, "\"message\":\"saved\"");
}

void handleApiPairingGet(AsyncWebServerRequest* req) {
    MqttConfig cfg{};
    if (!mqttCfgSnapshotTimed(&cfg, 2000U)) {
        sendErr(req, 503, "busy");
        return;
    }
    char deviceId[kDeviceIdBufLen]{};
    buildDeviceId(deviceId, sizeof(deviceId));
    char body[512]{};
    size_t pos = 0;
    int n = snprintf(body, sizeof(body), "{\"deviceId\":");
    if (n < 0 || static_cast<size_t>(n) >= sizeof(body)) {
        webSendEmpty(req, 500);
        return;
    }
    pos = static_cast<size_t>(n);
    if (!appendJsonStringQuotedEscaped(deviceId, body, sizeof(body), &pos)) {
        webSendEmpty(req, 500);
        return;
    }
    n = snprintf(body + pos, sizeof(body) - pos, ",\"partnerId\":");
    if (n < 0 || pos + static_cast<size_t>(n) >= sizeof(body)) {
        webSendEmpty(req, 500);
        return;
    }
    pos += static_cast<size_t>(n);
    if (!appendJsonStringQuotedEscaped(cfg.partnerDeviceId, body, sizeof(body), &pos)) {
        webSendEmpty(req, 500);
        return;
    }
    n = snprintf(body + pos, sizeof(body) - pos, ",\"topicPub\":");
    if (n < 0 || pos + static_cast<size_t>(n) >= sizeof(body)) {
        webSendEmpty(req, 500);
        return;
    }
    pos += static_cast<size_t>(n);
    if (!appendJsonStringQuotedEscaped(cfg.topicPub, body, sizeof(body), &pos)) {
        webSendEmpty(req, 500);
        return;
    }
    n = snprintf(body + pos, sizeof(body) - pos, ",\"topicSub\":");
    if (n < 0 || pos + static_cast<size_t>(n) >= sizeof(body)) {
        webSendEmpty(req, 500);
        return;
    }
    pos += static_cast<size_t>(n);
    if (!appendJsonStringQuotedEscaped(cfg.topicSub, body, sizeof(body), &pos)) {
        webSendEmpty(req, 500);
        return;
    }
    if (pos + 2U > sizeof(body)) {
        webSendEmpty(req, 500);
        return;
    }
    body[pos++] = '}';
    body[pos]   = '\0';
    webSendJson(req, 200, body);
}

void handleApiPairingPost(AsyncWebServerRequest* req) {
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
    if (!req->hasParam("partner_id", true)) {
        sendErr(req, 400, "partner");
        return;
    }
    const AsyncWebParameter* p = req->getParam("partner_id", true);
    if (p == nullptr) {
        sendErr(req, 400, "partner");
        return;
    }
    strlcpy(pending.partnerDeviceId, p->value().c_str(), sizeof(pending.partnerDeviceId));
    normalizePartnerIdInput(pending.partnerDeviceId, sizeof(pending.partnerDeviceId));
    if (!partnerIdInputValid(pending.partnerDeviceId)) {
        sendErr(req, 400, "partner");
        return;
    }
    mqttCfgApplyPairingTopics(&pending);
    if (!mqttTopicSyntaxOk(pending.topicPub, sizeof(pending.topicPub))
        || !mqttTopicSyntaxOk(pending.topicSub, sizeof(pending.topicSub))
        || strcmp(pending.topicPub, pending.topicSub) == 0) {
        sendErr(req, 400, "partner");
        return;
    }
    MqttConfig active{};
    if (!mqttCfgSnapshotTimed(&active, 2000U)) {
        sendErr(req, 503, "busy");
        return;
    }
    if (!mqttCfgEquals(&pending, &active)) {
        mqttCfgStorePending(&pending);
        g_webAdminMqttApplyVersion.fetch_add(1U, std::memory_order_acq_rel);
    }
    sendOk(req, 200, "\"message\":\"saved\"");
}

void handleApiSettingsGet(AsyncWebServerRequest* req) {
    char body[96];
    const int n =
        snprintf(body, sizeof(body), "{\"resetDays\":%u,\"authEnabled\":%s}",
                 static_cast<unsigned>(configGetResetPeriodDays()),
                 configGetWebAuthEnabled() ? "true" : "false");
    if (n < 0 || static_cast<size_t>(n) >= sizeof(body)) {
        webSendEmpty(req, 500);
        return;
    }
    webSendJson(req, 200, body);
}

void handleApiSettingsPost(AsyncWebServerRequest* req) {
    if (g_systemShutdownInProgress.load(std::memory_order_acquire)) {
        sendErr(req, 503, "shutdown");
        return;
    }
    uint8_t days = 7;
    if (req->hasParam("reset_days", true)) {
        const AsyncWebParameter* p = req->getParam("reset_days", true);
        if (p != nullptr) {
            const int v = p->value().toInt();
            days = (v >= 0 && v <= 30) ? static_cast<uint8_t>(v) : 7;
        }
    }
    portENTER_CRITICAL(&g_webAdminSettingsPendingMux);
    g_webAdminPendingResetDays   = days;
    g_webAdminPendingAuthEnabled = req->hasParam("auth_enabled", true);
    portEXIT_CRITICAL(&g_webAdminSettingsPendingMux);
    g_webAdminSettingsApplyPending.store(true, std::memory_order_release);
    sendOk(req, 200, "\"message\":\"saved\"");
}

void handleApiRebootPost(AsyncWebServerRequest* req) {
    if (g_systemShutdownInProgress.load(std::memory_order_acquire)) {
        sendErr(req, 503, "shutdown");
        return;
    }
    g_webAdminRebootRequested.store(true, std::memory_order_release);
    sendOk(req, 200, "\"message\":\"rebooting\"");
}

void handleApiUpdateCheckPost(AsyncWebServerRequest* req) {
    otaQueueGithubCheck();
    sendOk(req, 200, "\"message\":\"checking\"");
}

bool isSafeNextPath(const char* next) {
    if (next == nullptr || next[0] != '/') {
        return false;
    }
    if (strstr(next, "..") != nullptr) {
        return false;
    }
    if (strcmp(next, "/logout") == 0) {
        return false;
    }
    return true;
}

unsigned lockoutRemainingSec(unsigned long nowMs) {
    const unsigned long start = s_authLockoutStartMs.load(std::memory_order_acquire);
    if (start == 0UL || deadlineReached(start, kAuthLockoutMs, nowMs)) {
        return 0U;
    }
    const uint32_t remMs = remainingMs(start, kAuthLockoutMs, nowMs);
    const unsigned long sec = (static_cast<unsigned long>(remMs) + 999UL) / 1000UL;
    return static_cast<unsigned>(std::min(sec, static_cast<unsigned long>(7200)));
}

void handleApiAuthLoginPost(AsyncWebServerRequest* req) {
    if (!webRequestOriginAllowed(req)) {
        webSendEmpty(req, 403);
        return;
    }
    if (!configGetWebAuthEnabled() || configIsApMode()) {
        sendOk(req, 200, "\"next\":\"/\"");
        return;
    }
    const unsigned long nowMs = millis();
    const unsigned long lockoutStart = s_authLockoutStartMs.load(std::memory_order_acquire);
    if (lockoutStart != 0UL && !deadlineReached(lockoutStart, kAuthLockoutMs, nowMs)) {
        char body[96];
        snprintf(body, sizeof(body),
                 "{\"ok\":false,\"error\":\"lockout\",\"lockoutSec\":%u}",
                 lockoutRemainingSec(nowMs));
        webSendJson(req, 429, body);
        return;
    }
    if (!webAuthValidateCsrfPost(req) || !req->hasParam("code", true)) {
        sendErr(req, 401, "bad_code");
        return;
    }
    const AsyncWebParameter* p = req->getParam("code", true);
    const String& codeStr = (p != nullptr) ? p->value() : String();
    if (!webAuthCodeSyntaxOk(codeStr.c_str())) {
        sendErr(req, 401, "bad_code");
        return;
    }
    const uint32_t entered = static_cast<uint32_t>(strtoul(codeStr.c_str(), nullptr, 10));
    if (!tryConsumeAuthChallenge(entered, nowMs)) {
        const unsigned fails = s_authFailStreak.fetch_add(1, std::memory_order_acq_rel) + 1U;
        if (fails >= kAuthFailsForLock) {
            s_authLockoutStartMs.store(nowMs, std::memory_order_release);
            s_authFailStreak.store(0, std::memory_order_release);
        }
        sendErr(req, 401, "bad_code");
        return;
    }
    s_authFailStreak.store(0, std::memory_order_relaxed);
    portENTER_CRITICAL(&s_authMux);
    esp_fill_random(s_sessionRaw, sizeof(s_sessionRaw));
    s_sessionActive    = true;
    s_sessionCreatedMs = nowMs;
    rotateCsrfTokenLocked();
    char hexCookie[33];
    hexEncode16(s_sessionRaw, hexCookie);
    portEXIT_CRITICAL(&s_authMux);

    awaitingClearAtomic();
    challengeClearAtomic();
    buttonRequestAuthBlinkOffFromAsync();
    scheduleMainTaskScreenAfterAuthFlow();

    char nextPath[256] = "/";
    if (req->hasParam("next", true)) {
        const AsyncWebParameter* np = req->getParam("next", true);
        if (np != nullptr && isSafeNextPath(np->value().c_str())) {
            strlcpy(nextPath, np->value().c_str(), sizeof(nextPath));
        }
    }

    char body[320];
    size_t pos = 0;
    int n = snprintf(body, sizeof(body), "{\"ok\":true,\"next\":");
    if (n < 0 || static_cast<size_t>(n) >= sizeof(body)) {
        webSendEmpty(req, 500);
        return;
    }
    pos = static_cast<size_t>(n);
    if (!appendJsonStringQuotedEscaped(nextPath, body, sizeof(body), &pos)) {
        webSendEmpty(req, 500);
        return;
    }
    if (pos + 2U > sizeof(body)) {
        webSendEmpty(req, 500);
        return;
    }
    body[pos++] = '}';
    body[pos]   = '\0';

    AsyncWebServerResponse* resp = req->beginResponse(200, "application/json", body);
    char cookieBuf[160];
    snprintf(cookieBuf, sizeof(cookieBuf), "%s=%s; Path=/; HttpOnly; Max-Age=%lu; SameSite=Strict",
             kCookieName, hexCookie, static_cast<unsigned long>(kSessionCookieMaxAgeSec));
    resp->addHeader(F("Set-Cookie"), cookieBuf);
    webAddSecurityHeaders(resp);
    req->send(resp);
}

void handleApiAuthLogoutPost(AsyncWebServerRequest* req) {
    if (!configGetWebAuthEnabled() || configIsApMode()) {
        sendOk(req, 200, "\"next\":\"/\"");
        return;
    }
    webAuthInvalidateSession();
    AsyncWebServerResponse* resp =
        req->beginResponse(200, "application/json", "{\"ok\":true,\"next\":\"/auth\"}");
    resp->addHeader(F("Set-Cookie"), F("chaya_sid=; Path=/; HttpOnly; Max-Age=0; SameSite=Strict"));
    webAddSecurityHeaders(resp);
    req->send(resp);
}

} // namespace

void adminRoutesRegisterApi(AsyncWebServer& ws) {
    {
        AsyncCallbackWebHandler& h =
            ws.on("/api/csrf", HTTP_GET, [](AsyncWebServerRequest* rq) { handleApiCsrfGet(rq); });
        h.addMiddleware(mwRequireAllowedHost());
    }
    {
        AsyncCallbackWebHandler& h =
            ws.on("/api/device", HTTP_GET, [](AsyncWebServerRequest* rq) { handleApiDeviceGet(rq); });
        h.addMiddleware(mwRequireAllowedHost());
    }
    {
        AsyncCallbackWebHandler& h =
            ws.on("/api/chaya", HTTP_GET, [](AsyncWebServerRequest* rq) { handleApiChayaGet(rq); });
        h.addMiddleware(mwApiStaMode());
        h.addMiddleware(mwApiSessionGet());
    }
    {
        AsyncCallbackWebHandler& h = ws.on("/api/chaya/send", HTTP_POST,
                                           [](AsyncWebServerRequest* rq) { handleApiChayaSendPost(rq); });
        h.addMiddleware(mwApiStaMode());
        h.addMiddleware(mwApiPostSessionAndCsrf());
    }
    {
        AsyncCallbackWebHandler& h = ws.on("/api/wifi/status", HTTP_GET,
                                           [](AsyncWebServerRequest* rq) { handleApiWifiStatusGet(rq); });
        h.addMiddleware(mwApiWifiInfoOrApOpenGet());
    }
    {
        AsyncCallbackWebHandler& h = ws.on("/api/wifi/scan", HTTP_GET,
                                           [](AsyncWebServerRequest* rq) { handleApiWifiScanGet(rq); });
        h.addMiddleware(mwApiWifiInfoOrApOpenGet());
    }
    {
        AsyncCallbackWebHandler& h = ws.on("/api/wifi/connect", HTTP_POST,
                                           [](AsyncWebServerRequest* rq) { handleApiWifiConnectPost(rq); });
        h.addMiddleware(mwApiWifiConnectPostGuard());
    }
    {
        AsyncCallbackWebHandler& h = ws.on(
            "/api/wifi/connect-status", HTTP_GET,
            [](AsyncWebServerRequest* rq) { handleApiWifiConnectStatusGet(rq); });
        h.addMiddleware(mwApiApMode());
    }
    {
        AsyncCallbackWebHandler& h = ws.on(
            "/api/wifi/connect-commit", HTTP_POST,
            [](AsyncWebServerRequest* rq) { handleApiWifiConnectCommitPost(rq); });
        h.addMiddleware(mwApiApPostCsrf());
    }
    {
        AsyncCallbackWebHandler& h = ws.on(
            "/api/wifi/connect-abort", HTTP_POST,
            [](AsyncWebServerRequest* rq) { handleApiWifiConnectAbortPost(rq); });
        h.addMiddleware(mwApiApPostCsrf());
    }
    {
        AsyncCallbackWebHandler& h = ws.on("/api/mqtt/status", HTTP_GET,
                                           [](AsyncWebServerRequest* rq) { handleApiMqttStatusGet(rq); });
        h.addMiddleware(mwApiStaMode());
        h.addMiddleware(mwApiSessionGet());
    }
    {
        AsyncCallbackWebHandler& h =
            ws.on("/api/mqtt", HTTP_GET, [](AsyncWebServerRequest* rq) { handleApiMqttGet(rq); });
        h.addMiddleware(mwApiStaMode());
        h.addMiddleware(mwApiSessionGet());
    }
    {
        AsyncCallbackWebHandler& h =
            ws.on("/api/mqtt", HTTP_POST, [](AsyncWebServerRequest* rq) { handleApiMqttPost(rq); });
        h.addMiddleware(mwApiStaMode());
        h.addMiddleware(mwApiPostSessionAndCsrf());
    }
    {
        AsyncCallbackWebHandler& h =
            ws.on("/api/pairing", HTTP_GET, [](AsyncWebServerRequest* rq) { handleApiPairingGet(rq); });
        h.addMiddleware(mwApiStaMode());
        h.addMiddleware(mwApiSessionGet());
    }
    {
        AsyncCallbackWebHandler& h = ws.on("/api/pairing", HTTP_POST,
                                           [](AsyncWebServerRequest* rq) { handleApiPairingPost(rq); });
        h.addMiddleware(mwApiStaMode());
        h.addMiddleware(mwApiPostSessionAndCsrf());
    }
    {
        AsyncCallbackWebHandler& h = ws.on("/api/settings", HTTP_GET,
                                           [](AsyncWebServerRequest* rq) { handleApiSettingsGet(rq); });
        h.addMiddleware(mwApiStaMode());
        h.addMiddleware(mwApiSessionGet());
    }
    {
        AsyncCallbackWebHandler& h = ws.on("/api/settings", HTTP_POST,
                                           [](AsyncWebServerRequest* rq) { handleApiSettingsPost(rq); });
        h.addMiddleware(mwApiStaMode());
        h.addMiddleware(mwApiPostSessionAndCsrf());
    }
    {
        AsyncCallbackWebHandler& h =
            ws.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest* rq) { handleApiRebootPost(rq); });
        h.addMiddleware(mwApiStaMode());
        h.addMiddleware(mwApiPostSessionAndCsrf());
    }
    {
        AsyncCallbackWebHandler& h = ws.on("/api/update/check", HTTP_POST,
                                           [](AsyncWebServerRequest* rq) { handleApiUpdateCheckPost(rq); });
        h.addMiddleware(mwApiStaMode());
        h.addMiddleware(mwApiPostSessionAndCsrf());
    }
    {
        AsyncCallbackWebHandler& h = ws.on("/api/auth/login", HTTP_POST,
                                           [](AsyncWebServerRequest* rq) { handleApiAuthLoginPost(rq); });
        h.addMiddleware(mwRequireAllowedHost());
    }
    {
        AsyncCallbackWebHandler& h = ws.on("/api/auth/logout", HTTP_POST,
                                           [](AsyncWebServerRequest* rq) { handleApiAuthLogoutPost(rq); });
        h.addMiddleware(mwApiPostSessionAndCsrf());
    }
}
