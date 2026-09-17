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
#include "sse_send_pure.h"
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
// s_wifiNetCacheMux protects the last-good STA snapshot (HTTP bootstrap + SSE).
// PERF-03: producers mark dirty bits; idle clients skip gather until keepalive.

namespace {

AsyncEventSource s_events("/events");

constexpr size_t kMaxSseClients = 6U;
constexpr uint32_t kSseKeepaliveMs = 8000U;

// ESP32Async connect gate: client cap. Host is the server middleware.
AsyncAuthorizationMiddleware s_sseConnectGate([](AsyncWebServerRequest *) { return s_events.count() < kMaxSseClients; });

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
WifiStaNetCache s_wifiNetCache{};
portMUX_TYPE s_wifiNetCacheMux = portMUX_INITIALIZER_UNLOCKED;

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

/** Serialize must fit; send must enqueue. RC-WEB-13: only ENQUEUED is success; PARTIALLY_ENQUEUED must redirty. */
static bool sseSendEvent(const char *event, const char *buf, size_t n, size_t bufLen) {
    if (event == nullptr || buf == nullptr || n == 0U || n >= bufLen) {
        return false;
    }
    return s_events.send(buf, event) == AsyncEventSource::ENQUEUED;
}

} // namespace

bool webWifiStaNetSnapshotOrCached(WifiStaNetFields *out) {
    if (out == nullptr) {
        return false;
    }
    WifiStaNetFields live{};
    const bool snapOk = wlanFillStaNetSnapshot(&live.connected, live.ssid, sizeof(live.ssid), live.ip, sizeof(live.ip),
                                               live.gateway, sizeof(live.gateway), live.netmask, sizeof(live.netmask), live.dns1,
                                               sizeof(live.dns1), live.dns2, sizeof(live.dns2), &live.rssi);
    portENTER_CRITICAL(&s_wifiNetCacheMux);
    const WifiStaNetResolve resolved = wifiStaNetResolveSnapshot(snapOk, &s_wifiNetCache, &live);
    portEXIT_CRITICAL(&s_wifiNetCacheMux);
    if (resolved == WifiStaNetResolve::None) {
        return false;
    }
    *out = live;
    return true;
}

void webEventsRegister(AsyncWebServer &ws) {
    s_events.addMiddleware(&s_sseConnectGate);
    s_events.onConnect(onEsConnect);
    ws.addHandler(&s_events);
}

void webEventsTick() {
    if (s_events.count() == 0) {
        return;
    }

    static uint32_t s_lastWorkMs = 0U;

    const uint32_t nowMs = millis();
    const uint32_t pending = sseConsumeDirty();
    bool keepalive = false;
    const uint32_t selected = sseTickSelectBits(pending, nowMs, s_lastWorkMs, kSseKeepaliveMs, &keepalive);
    const bool force = sseTickForceSnapshot(selected);
    const uint32_t workBits = sseTickMaskForApMode(selected, configIsApMode());
    if (workBits == 0U) {
        return;
    }
    s_lastWorkMs = nowMs;

    const bool wantChaya = (workBits & kSseChaya) != 0U;
    const bool wantWifi = (workBits & kSseWifi) != 0U;
    const bool wantMqtt = (workBits & kSseMqtt) != 0U;
    const bool wantOta = (workBits & kSseOta) != 0U;
    const bool wantDevice = (workBits & kSseDevice) != 0U;

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
        heartCounterFillChayaDeltas(&rx, &tx);
    }

    WifiStaNetFields wifi{};
    if (wantWifi) {
        (void)webWifiStaNetSnapshotOrCached(&wifi);
    }

    const bool mqttConnNow = mqttPageConn(mqttPageRelevant, mqttLineOk);

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

    // Compare only — last-sent cache is written after a successful send (RC-WEB-04).
    bool chayaDirty = false;
    bool wifiDirty = false;
    bool mqttStatusDirty = force;
    bool otaDirty = force;
    bool deviceDirty = force;
    portENTER_CRITICAL(&s_esCacheMux);
    if (wantChaya) {
        chayaDirty = force || !s_haveLastChaya || s_lastRx != rx || s_lastTx != tx || s_lastMqttConn != mqttLineOk ||
                     s_lastMqttConfigured != mqttPageRelevant || s_lastMqttPaired != mqttPaired;
    }
    if (wantWifi) {
        wifiDirty = force || keepalive || !s_haveLastWifi || s_lastWifiConnected != wifi.connected ||
                    s_lastWifiRssi != wifi.rssi || strcmp(s_lastWifiSsid, wifi.ssid) != 0 || strcmp(s_lastWifiIp, wifi.ip) != 0 ||
                    strcmp(s_lastWifiGateway, wifi.gateway) != 0 || strcmp(s_lastWifiNetmask, wifi.netmask) != 0 ||
                    strcmp(s_lastWifiDns1, wifi.dns1) != 0 || strcmp(s_lastWifiDns2, wifi.dns2) != 0;
    }
    if (wantMqtt) {
        if (mqttPageRelevant) {
            if (!s_haveLastMqttStatus || s_lastMqttPageConn != mqttConnNow) {
                mqttStatusDirty = true;
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
    }
    if (wantDevice) {
        if (!s_haveLastDevice || s_lastBatteryPct != batteryPct) {
            deviceDirty = true;
        }
    }
    portEXIT_CRITICAL(&s_esCacheMux);

    char buf[640];

    if (chayaDirty) {
        const size_t n = buildChayaPayload(rx, tx, mqttLineOk, mqttPageRelevant, mqttPaired, buf, sizeof(buf));
        if (sseSendEvent("chaya", buf, n, sizeof(buf))) {
            portENTER_CRITICAL(&s_esCacheMux);
            s_haveLastChaya = true;
            s_lastRx = rx;
            s_lastTx = tx;
            s_lastMqttConn = mqttLineOk;
            s_lastMqttConfigured = mqttPageRelevant;
            s_lastMqttPaired = mqttPaired;
            portEXIT_CRITICAL(&s_esCacheMux);
        } else {
            sseMarkDirty(kSseChaya);
        }
    }

    if (wifiDirty) {
        size_t plen = 0;
        if (wifi.connected) {
            plen = buildWifiStatusPayload(true, wifi.ssid, wifi.ip, wifi.gateway, wifi.netmask, wifi.dns1, wifi.dns2, wifi.rssi,
                                          buf, sizeof(buf));
        } else {
            plen = buildWifiStatusPayload(false, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, buf, sizeof(buf));
        }
        if (sseSendEvent("wifi", buf, plen, sizeof(buf))) {
            portENTER_CRITICAL(&s_esCacheMux);
            s_haveLastWifi = true;
            s_lastWifiConnected = wifi.connected;
            strlcpy(s_lastWifiSsid, wifi.ssid, sizeof(s_lastWifiSsid));
            strlcpy(s_lastWifiIp, wifi.ip, sizeof(s_lastWifiIp));
            strlcpy(s_lastWifiGateway, wifi.gateway, sizeof(s_lastWifiGateway));
            strlcpy(s_lastWifiNetmask, wifi.netmask, sizeof(s_lastWifiNetmask));
            strlcpy(s_lastWifiDns1, wifi.dns1, sizeof(s_lastWifiDns1));
            strlcpy(s_lastWifiDns2, wifi.dns2, sizeof(s_lastWifiDns2));
            s_lastWifiRssi = wifi.rssi;
            portEXIT_CRITICAL(&s_esCacheMux);
        } else {
            sseMarkDirty(kSseWifi);
        }
    }

    if (mqttStatusDirty && mqttPageRelevant) {
        const size_t plen = buildMqttStatusPayload(mqttConnNow, buf, sizeof(buf));
        if (sseSendEvent("mqtt", buf, plen, sizeof(buf))) {
            portENTER_CRITICAL(&s_esCacheMux);
            s_haveLastMqttStatus = true;
            s_lastMqttPageConn = mqttConnNow;
            portEXIT_CRITICAL(&s_esCacheMux);
        } else {
            sseMarkDirty(kSseMqtt);
        }
    }

    if (otaDirty) {
        JsonDocument doc;
        otaFillStatusJson(doc.to<JsonObject>(), otaSt);
        const size_t plen = webSerializeJson(doc, buf, sizeof(buf));
        if (sseSendEvent("ota", buf, plen, sizeof(buf))) {
            portENTER_CRITICAL(&s_esCacheMux);
            s_haveLastOta = true;
            s_lastOtaGeneration = otaSt.generation;
            portEXIT_CRITICAL(&s_esCacheMux);
        } else {
            sseMarkDirty(kSseOta);
        }
    }

    if (deviceDirty) {
        const size_t plen = buildDeviceBatteryPayload(batteryMv, batteryPct, buf, sizeof(buf));
        if (sseSendEvent("device", buf, plen, sizeof(buf))) {
            portENTER_CRITICAL(&s_esCacheMux);
            s_haveLastDevice = true;
            s_lastBatteryPct = batteryPct;
            portEXIT_CRITICAL(&s_esCacheMux);
        } else {
            sseMarkDirty(kSseDevice);
        }
    }
}
