#include "events.h"

#include "heart/counter.h"
#include "battery/battery.h"
#include "mqtt/config.h"
#include "mqtt/mqtt.h"
#include "ota/ota.h"
#include "wifi/wlan.h"
#include "web_utils.h"

#include <Arduino.h>
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

namespace {

AsyncEventSource s_events("/events");

constexpr size_t kMaxSseClients = 6U;

std::atomic<bool> s_forceBroadcast{false};
std::atomic<bool> s_loggedFirstSseClient{false};

bool     s_haveLastChaya      = false;
int      s_lastRx             = 0;
int      s_lastTx             = 0;
bool     s_lastMqttConn       = false;
bool     s_lastMqttConfigured = false;
bool     s_haveLastWifi       = false;
bool     s_lastWifiConnected  = false;
char     s_lastWifiSsid[kWifiSsidMaxLen]{};
char     s_lastWifiIp[16]{};
char     s_lastWifiGateway[16]{};
char     s_lastWifiNetmask[16]{};
char     s_lastWifiDns1[16]{};
char     s_lastWifiDns2[16]{};
int      s_lastWifiRssi       = 0;
bool     s_haveLastMqttStatus = false;
bool     s_lastMqttPageConn   = false;
bool     s_haveLastOta        = false;
uint32_t s_lastOtaGeneration  = 0;
bool     s_haveLastDevice     = false;
int      s_lastBatteryPct     = INT_MIN;

portMUX_TYPE s_esCacheMux = portMUX_INITIALIZER_UNLOCKED;

static void onEsConnect(AsyncEventSourceClient*) {
    if (!s_loggedFirstSseClient.exchange(true, std::memory_order_acq_rel)) {
        ESP_LOGD(TAG, "SSE: first client connected");
    }
    s_forceBroadcast.store(true, std::memory_order_release);
}

static size_t buildChayaPayload(int rx, int tx, bool connected, bool configured, char* buf,
                                size_t bufLen) {
    return static_cast<size_t>(snprintf(
        buf, bufLen, "{\"rx\":%d,\"tx\":%d,\"connected\":%s,\"configured\":%s}", rx, tx,
        connected ? "true" : "false", configured ? "true" : "false"));
}

static size_t buildWifiStatusPayload(bool connected, const char* ssid, const char* ipStr,
                                     const char* gateway, const char* netmask, const char* dns1,
                                     const char* dns2, int rssi, char* buf, size_t bufLen) {
    if (!connected) {
        return static_cast<size_t>(snprintf(buf, bufLen, "{\"connected\":false}"));
    }
    size_t pos = 0;
    const int head = snprintf(buf, bufLen, "{\"connected\":true,\"ssid\":");
    if (head < 0 || static_cast<size_t>(head) >= bufLen) {
        return 0;
    }
    pos = static_cast<size_t>(head);
    if (!appendJsonStringQuotedEscaped(ssid != nullptr ? ssid : "", buf, bufLen, &pos)) {
        return 0;
    }
    const size_t remain = (bufLen > pos) ? (bufLen - pos) : 0;
    const int tail = snprintf(
        buf + pos, remain,
        ",\"ip\":\"%s\",\"gateway\":\"%s\",\"netmask\":\"%s\",\"dns1\":\"%s\",\"dns2\":\"%s\","
        "\"rssi\":%d}",
        ipStr != nullptr ? ipStr : "", gateway != nullptr ? gateway : "",
        netmask != nullptr ? netmask : "", dns1 != nullptr ? dns1 : "",
        dns2 != nullptr ? dns2 : "", rssi);
    if (tail < 0 || static_cast<size_t>(tail) >= remain) {
        return 0;
    }
    return pos + static_cast<size_t>(tail);
}

static size_t buildMqttStatusPayload(bool connected, char* buf, size_t bufLen) {
    return static_cast<size_t>(
        snprintf(buf, bufLen, "{\"connected\":%s}", connected ? "true" : "false"));
}

static size_t buildDeviceBatteryPayload(int mv, int pct, char* buf, size_t bufLen) {
    return static_cast<size_t>(
        snprintf(buf, bufLen, "{\"batteryMv\":%d,\"batteryPct\":%d}", mv, pct));
}

} // namespace

void webEventsRegister(AsyncWebServer& ws) {
    s_events.authorizeConnect([](AsyncWebServerRequest* req) {
        if (!webRequestHostAllowed(req) || !webRequestOriginAllowed(req)) {
            return false;
        }
        if (s_events.count() >= kMaxSseClients) {
            return false;
        }
        return true;
    });
    s_events.onConnect(onEsConnect);
    ws.addHandler(&s_events);
}

void webEventsTick() {
    if (s_events.count() == 0) {
        return;
    }

    static unsigned s_wifiRfSkip     = 99U;
    static bool     s_cachedWifiConn = false;
    static char     s_cachedCurSsid[kWifiSsidMaxLen]{};
    static char     s_cachedCurIp[16]{};
    static char     s_cachedGateway[16]{};
    static char     s_cachedNetmask[16]{};
    static char     s_cachedDns1[16]{};
    static char     s_cachedDns2[16]{};
    static int      s_cachedRssi     = 0;

    const int   rx                = heartDisplayRxDelta();
    const int   tx                = heartDisplayTxDelta();
    const bool  mqttLineOk        = mqttIsConnected();
    const bool  mqttPageRelevant  = mqttCfgIsBrokerConfigured();
    const bool  force             = s_forceBroadcast.exchange(false, std::memory_order_acq_rel);
    if (force) {
        s_wifiRfSkip = 99U;
    }

    bool wifiConn = false;
    char curSsid[kWifiSsidMaxLen]{};
    char curIp[16]{};
    char curGateway[16]{};
    char curNetmask[16]{};
    char curDns1[16]{};
    char curDns2[16]{};
    int  rssi = 0;
    if (s_wifiRfSkip >= 4U) {
        s_wifiRfSkip = 0U;
        if (wlanFillStaNetSnapshot(&wifiConn, curSsid, sizeof(curSsid), curIp, sizeof(curIp),
                                   curGateway, sizeof(curGateway), curNetmask, sizeof(curNetmask),
                                   curDns1, sizeof(curDns1), curDns2, sizeof(curDns2), &rssi)) {
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
    } else {
        ++s_wifiRfSkip;
        wifiConn = s_cachedWifiConn;
        strlcpy(curSsid, s_cachedCurSsid, sizeof(curSsid));
        strlcpy(curIp, s_cachedCurIp, sizeof(curIp));
        strlcpy(curGateway, s_cachedGateway, sizeof(curGateway));
        strlcpy(curNetmask, s_cachedNetmask, sizeof(curNetmask));
        strlcpy(curDns1, s_cachedDns1, sizeof(curDns1));
        strlcpy(curDns2, s_cachedDns2, sizeof(curDns2));
        rssi = s_cachedRssi;
    }

    const bool mqttConnNow = mqttPageRelevant ? mqttIsConnected() : false;

    OtaStatus otaSt{};
    otaCopyStatus(&otaSt);

    const int batteryMv  = batteryMilliVolts();
    const int batteryPct = batteryPercent();

    bool chayaDirty      = false;
    bool wifiDirty       = false;
    bool mqttStatusDirty = force;
    bool otaDirty        = force;
    bool deviceDirty     = force;
    portENTER_CRITICAL(&s_esCacheMux);
    chayaDirty = force || !s_haveLastChaya || s_lastRx != rx || s_lastTx != tx
              || s_lastMqttConn != mqttLineOk || s_lastMqttConfigured != mqttPageRelevant;
    if (chayaDirty) {
        s_haveLastChaya      = true;
        s_lastRx             = rx;
        s_lastTx             = tx;
        s_lastMqttConn       = mqttLineOk;
        s_lastMqttConfigured = mqttPageRelevant;
    }
    wifiDirty =
        force || !s_haveLastWifi || s_lastWifiConnected != wifiConn || s_lastWifiRssi != rssi
        || strcmp(s_lastWifiSsid, curSsid) != 0 || strcmp(s_lastWifiIp, curIp) != 0
        || strcmp(s_lastWifiGateway, curGateway) != 0 || strcmp(s_lastWifiNetmask, curNetmask) != 0
        || strcmp(s_lastWifiDns1, curDns1) != 0 || strcmp(s_lastWifiDns2, curDns2) != 0;
    if (wifiDirty) {
        s_haveLastWifi      = true;
        s_lastWifiConnected = wifiConn;
        strlcpy(s_lastWifiSsid, curSsid, sizeof(s_lastWifiSsid));
        strlcpy(s_lastWifiIp, curIp, sizeof(s_lastWifiIp));
        strlcpy(s_lastWifiGateway, curGateway, sizeof(s_lastWifiGateway));
        strlcpy(s_lastWifiNetmask, curNetmask, sizeof(s_lastWifiNetmask));
        strlcpy(s_lastWifiDns1, curDns1, sizeof(s_lastWifiDns1));
        strlcpy(s_lastWifiDns2, curDns2, sizeof(s_lastWifiDns2));
        s_lastWifiRssi = rssi;
    }
    if (mqttPageRelevant) {
        if (!s_haveLastMqttStatus || s_lastMqttPageConn != mqttConnNow) {
            mqttStatusDirty = true;
        }
        if (mqttStatusDirty) {
            s_haveLastMqttStatus = true;
            s_lastMqttPageConn   = mqttConnNow;
        }
    } else {
        s_haveLastMqttStatus = false;
    }
    if (!s_haveLastOta || s_lastOtaGeneration != otaSt.generation) {
        otaDirty = true;
    }
    if (otaDirty) {
        s_haveLastOta       = true;
        s_lastOtaGeneration = otaSt.generation;
    }
    if (!s_haveLastDevice || s_lastBatteryPct != batteryPct) {
        deviceDirty = true;
    }
    if (deviceDirty) {
        s_haveLastDevice = true;
        s_lastBatteryPct = batteryPct;
    }
    portEXIT_CRITICAL(&s_esCacheMux);

    char buf[640];

    if (chayaDirty) {
        const size_t n =
            buildChayaPayload(rx, tx, mqttLineOk, mqttPageRelevant, buf, sizeof(buf));
        if (n > 0U && n < sizeof(buf)) {
            s_events.send(buf, "chaya");
        }
    }

    if (wifiDirty) {
        size_t plen = 0;
        if (wifiConn) {
            plen = buildWifiStatusPayload(true, curSsid, curIp, curGateway, curNetmask, curDns1,
                                          curDns2, rssi, buf, sizeof(buf));
        } else {
            plen = buildWifiStatusPayload(false, nullptr, nullptr, nullptr, nullptr, nullptr,
                                          nullptr, 0, buf, sizeof(buf));
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
        const size_t plen = otaFormatStatusJson(buf, sizeof(buf));
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
