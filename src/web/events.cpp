#include "events.h"

#include "async/sse_dirty.h"
#include "battery/battery.h"
#include "heart/counter.h"
#include "json_payloads.h"
#include "mqtt/config.h"
#include "mqtt/mqtt.h"
#include "ota/ota.h"
#include "ota/ota_json.h"
#include "sse_dirty_pure.h"
#include "web_utils.h"
#include "wifi/wlan.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <esp_log.h>

#include <atomic>
#include <climits>
#include <cstdio>
#include <cstring>

#include "util/log_tag.h"

DEFINE_LOG_TAG("SSE");

// Release builds strip ESP_LOG*; TAG uses __attribute__((unused)) via DEFINE_LOG_TAG.

// /events SSE hub for dashboard scripts.
// s_esCacheMux protects last-sent caches; webEventsTick() is the sole writer.
// PERF-03: producers mark dirty bits; idle clients skip gather until keepalive.

namespace {

AsyncEventSource s_events("/events");

constexpr size_t kMaxSseClients = 6U;
constexpr uint32_t kSseKeepaliveMs = 8000U;

// ESP32Async 3.12: authorizeConnect is superseded by AsyncAuthorizationMiddleware.
AsyncAuthorizationMiddleware s_sseAuth([](AsyncWebServerRequest *req) {
    if (!webRequestHostAllowed(req)) {
        return false;
    }
    return s_events.count() < kMaxSseClients;
});

std::atomic<bool> s_loggedFirstSseClient{false};

bool s_haveLastChaya = false;
int s_lastRx = 0;
int s_lastTx = 0;
bool s_lastMqttConn = false;
bool s_lastMqttConfigured = false;
bool s_lastMqttPaired = false;
bool s_haveLastWifi = false;
bool s_lastWifiConnected = false;
char s_lastWifiSsid[kWifiSsidMaxLen]{};
char s_lastWifiIp[16]{};
char s_lastWifiGateway[16]{};
char s_lastWifiNetmask[16]{};
char s_lastWifiDns1[16]{};
char s_lastWifiDns2[16]{};
int s_lastWifiRssi = 0;
bool s_haveLastMqttStatus = false;
bool s_lastMqttPageConn = false;
bool s_haveLastOta = false;
uint32_t s_lastOtaGeneration = 0;
bool s_haveLastDevice = false;
int s_lastBatteryPct = INT_MIN;

portMUX_TYPE s_esCacheMux = portMUX_INITIALIZER_UNLOCKED;

static void onEsConnect(AsyncEventSourceClient *) {
    if (!s_loggedFirstSseClient.exchange(true, std::memory_order_acq_rel)) {
        ESP_LOGD(TAG, "SSE: first client connected");
    }
    sseMarkDirty(kSseAll);
}

static size_t buildChayaPayload(int rx, int tx, bool connected, bool configured, bool paired, char *buf, size_t bufLen) {
    JsonDocument doc;
    fillChayaJson(doc.to<JsonObject>(), rx, tx, connected, configured, paired);
    return webSerializeJson(doc, buf, bufLen);
}

static size_t buildWifiStatusPayload(bool connected, const char *ssid, const char *ipStr, const char *gateway,
                                     const char *netmask, const char *dns1, const char *dns2, int rssi, char *buf,
                                     size_t bufLen) {
    JsonDocument doc;
    fillWifiStatusJson(doc.to<JsonObject>(), connected, ssid, ipStr, gateway, netmask, dns1, dns2, rssi);
    return webSerializeJson(doc, buf, bufLen);
}

static size_t buildMqttStatusPayload(bool connected, char *buf, size_t bufLen) {
    JsonDocument doc;
    fillMqttStatusJson(doc.to<JsonObject>(), connected);
    return webSerializeJson(doc, buf, bufLen);
}

static size_t buildDeviceBatteryPayload(int mv, int pct, char *buf, size_t bufLen) {
    JsonDocument doc;
    fillDeviceBatteryJson(doc.to<JsonObject>(), mv, pct);
    return webSerializeJson(doc, buf, bufLen);
}

} // namespace

void webEventsRegister(AsyncWebServer &ws) {
    s_events.addMiddleware(&s_sseAuth);
    s_events.onConnect(onEsConnect);
    ws.addHandler(&s_events);
}

void webEventsTick() {
    if (s_events.count() == 0) {
        return;
    }

    static uint32_t s_lastWorkMs = 0U;
    static bool s_cachedWifiConn = false;
    static char s_cachedCurSsid[kWifiSsidMaxLen]{};
    static char s_cachedCurIp[16]{};
    static char s_cachedGateway[16]{};
    static char s_cachedNetmask[16]{};
    static char s_cachedDns1[16]{};
    static char s_cachedDns2[16]{};
    static int s_cachedRssi = 0;

    const uint32_t nowMs = millis();
    const uint32_t pending = sseConsumeDirty();
    bool keepalive = false;
    const uint32_t workBits = sseTickSelectBits(pending, nowMs, s_lastWorkMs, kSseKeepaliveMs, &keepalive);
    if (workBits == 0U) {
        return;
    }
    s_lastWorkMs = nowMs;

    const bool wantChaya = (workBits & kSseChaya) != 0U;
    const bool wantWifi = (workBits & kSseWifi) != 0U;
    const bool wantMqtt = (workBits & kSseMqtt) != 0U;
    const bool wantOta = (workBits & kSseOta) != 0U;
    const bool wantDevice = (workBits & kSseDevice) != 0U;
    const bool force = (workBits & kSseAll) == kSseAll;

    int rx = 0;
    int tx = 0;
    bool mqttLineOk = false;
    bool mqttPageRelevant = false;
    bool mqttPaired = false;
    if (wantChaya || wantMqtt) {
        mqttLineOk = mqttIsConnected();
        mqttPageRelevant = mqttCfgIsBrokerConfigured();
        mqttPaired = mqttCfgIsPaired();
    }
    if (wantChaya) {
        rx = heartDisplayRxDelta();
        tx = heartDisplayTxDelta();
    }

    bool wifiConn = false;
    char curSsid[kWifiSsidMaxLen]{};
    char curIp[16]{};
    char curGateway[16]{};
    char curNetmask[16]{};
    char curDns1[16]{};
    char curDns2[16]{};
    int rssi = 0;
    if (wantWifi) {
        if (wlanFillStaNetSnapshot(&wifiConn, curSsid, sizeof(curSsid), curIp, sizeof(curIp), curGateway, sizeof(curGateway),
                                   curNetmask, sizeof(curNetmask), curDns1, sizeof(curDns1), curDns2, sizeof(curDns2), &rssi)) {
            s_cachedWifiConn = wifiConn;
            strlcpy(s_cachedCurSsid, curSsid, sizeof(s_cachedCurSsid));
            strlcpy(s_cachedCurIp, curIp, sizeof(s_cachedCurIp));
            strlcpy(s_cachedGateway, curGateway, sizeof(s_cachedGateway));
            strlcpy(s_cachedNetmask, curNetmask, sizeof(s_cachedNetmask));
            strlcpy(s_cachedDns1, curDns1, sizeof(s_cachedDns1));
            strlcpy(s_cachedDns2, curDns2, sizeof(s_cachedDns2));
            s_cachedRssi = rssi;
        } else {
            wifiConn = s_cachedWifiConn;
            strlcpy(curSsid, s_cachedCurSsid, sizeof(curSsid));
            strlcpy(curIp, s_cachedCurIp, sizeof(curIp));
            strlcpy(curGateway, s_cachedGateway, sizeof(curGateway));
            strlcpy(curNetmask, s_cachedNetmask, sizeof(curNetmask));
            strlcpy(curDns1, s_cachedDns1, sizeof(curDns1));
            strlcpy(curDns2, s_cachedDns2, sizeof(curDns2));
            rssi = s_cachedRssi;
        }
    }

    const bool mqttConnNow = mqttPageRelevant ? mqttIsConnected() : false;

    OtaStatus otaSt{};
    if (wantOta) {
        otaCopyStatus(&otaSt);
    }

    int batteryMv = 0;
    int batteryPct = 0;
    if (wantDevice) {
        batteryMv = batteryMilliVolts();
        batteryPct = batteryPercent();
    }

    bool chayaDirty = false;
    bool wifiDirty = false;
    bool mqttStatusDirty = force;
    bool otaDirty = force;
    bool deviceDirty = force;
    portENTER_CRITICAL(&s_esCacheMux);
    if (wantChaya) {
        chayaDirty = force || !s_haveLastChaya || s_lastRx != rx || s_lastTx != tx || s_lastMqttConn != mqttLineOk ||
                     s_lastMqttConfigured != mqttPageRelevant || s_lastMqttPaired != mqttPaired;
        if (chayaDirty) {
            s_haveLastChaya = true;
            s_lastRx = rx;
            s_lastTx = tx;
            s_lastMqttConn = mqttLineOk;
            s_lastMqttConfigured = mqttPageRelevant;
            s_lastMqttPaired = mqttPaired;
        }
    }
    if (wantWifi) {
        wifiDirty = force || keepalive || !s_haveLastWifi || s_lastWifiConnected != wifiConn || s_lastWifiRssi != rssi ||
                    strcmp(s_lastWifiSsid, curSsid) != 0 || strcmp(s_lastWifiIp, curIp) != 0 ||
                    strcmp(s_lastWifiGateway, curGateway) != 0 || strcmp(s_lastWifiNetmask, curNetmask) != 0 ||
                    strcmp(s_lastWifiDns1, curDns1) != 0 || strcmp(s_lastWifiDns2, curDns2) != 0;
        if (wifiDirty) {
            s_haveLastWifi = true;
            s_lastWifiConnected = wifiConn;
            strlcpy(s_lastWifiSsid, curSsid, sizeof(s_lastWifiSsid));
            strlcpy(s_lastWifiIp, curIp, sizeof(s_lastWifiIp));
            strlcpy(s_lastWifiGateway, curGateway, sizeof(s_lastWifiGateway));
            strlcpy(s_lastWifiNetmask, curNetmask, sizeof(s_lastWifiNetmask));
            strlcpy(s_lastWifiDns1, curDns1, sizeof(s_lastWifiDns1));
            strlcpy(s_lastWifiDns2, curDns2, sizeof(s_lastWifiDns2));
            s_lastWifiRssi = rssi;
        }
    }
    if (wantMqtt) {
        if (mqttPageRelevant) {
            if (!s_haveLastMqttStatus || s_lastMqttPageConn != mqttConnNow) {
                mqttStatusDirty = true;
            }
            if (mqttStatusDirty) {
                s_haveLastMqttStatus = true;
                s_lastMqttPageConn = mqttConnNow;
            }
        } else {
            s_haveLastMqttStatus = false;
            mqttStatusDirty = false;
        }
    }
    if (wantOta) {
        if (!s_haveLastOta || s_lastOtaGeneration != otaSt.generation) {
            otaDirty = true;
        }
        if (otaDirty) {
            s_haveLastOta = true;
            s_lastOtaGeneration = otaSt.generation;
        }
    }
    if (wantDevice) {
        if (!s_haveLastDevice || s_lastBatteryPct != batteryPct) {
            deviceDirty = true;
        }
        if (deviceDirty) {
            s_haveLastDevice = true;
            s_lastBatteryPct = batteryPct;
        }
    }
    portEXIT_CRITICAL(&s_esCacheMux);

    char buf[640];

    if (chayaDirty) {
        const size_t n = buildChayaPayload(rx, tx, mqttLineOk, mqttPageRelevant, mqttPaired, buf, sizeof(buf));
        if (n > 0U && n < sizeof(buf)) {
            s_events.send(buf, "chaya");
        }
    }

    if (wifiDirty) {
        size_t plen = 0;
        if (wifiConn) {
            plen = buildWifiStatusPayload(true, curSsid, curIp, curGateway, curNetmask, curDns1, curDns2, rssi, buf, sizeof(buf));
        } else {
            plen = buildWifiStatusPayload(false, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, buf, sizeof(buf));
        }
        if (plen > 0U && plen < sizeof(buf)) {
            s_events.send(buf, "wifi");
        }
    }

    if (mqttStatusDirty && mqttPageRelevant) {
        const size_t plen = buildMqttStatusPayload(mqttConnNow, buf, sizeof(buf));
        if (plen > 0U && plen < sizeof(buf)) {
            s_events.send(buf, "mqtt");
        }
    }

    if (otaDirty) {
        JsonDocument doc;
        otaFillStatusJson(doc.to<JsonObject>(), otaSt);
        const size_t plen = webSerializeJson(doc, buf, sizeof(buf));
        if (plen > 0U && plen < sizeof(buf)) {
            s_events.send(buf, "ota");
        }
    }

    if (deviceDirty) {
        const size_t plen = buildDeviceBatteryPayload(batteryMv, batteryPct, buf, sizeof(buf));
        if (plen > 0U && plen < sizeof(buf)) {
            s_events.send(buf, "device");
        }
    }
}
