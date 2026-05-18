#include "web_events.h"

#include "auth.h"
#include "config/app_config.h"
#include "constants.h"
#include "heart/counter.h"
#include "mqtt/config.h"
#include "mqtt/mqtt.h"
#include "wifi/wlan.h"
#include "web_utils.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

#include <atomic>
#include <climits>
#include <cstdio>
#include <cstring>

// /events SSE hub for dashboard scripts.

namespace {

AsyncEventSource g_events("/events");

std::atomic<bool> g_forceBroadcast{false};

bool     g_haveLastChaya      = false;
int      g_lastRx             = 0;
int      g_lastTx             = 0;
bool     g_lastMqttConn       = false;
bool     g_haveLastWifi       = false;
bool     g_lastWifiConnected  = false;
char     g_lastWifiSsid[kWifiSsidMaxLen]{};
char     g_lastWifiIp[16]{};
int      g_lastWifiRssi       = 0;
bool     g_haveLastMqttStatus = false;
bool     g_lastMqttPageConn   = false;

portMUX_TYPE g_esCacheMux = portMUX_INITIALIZER_UNLOCKED;

static void onEsConnect(AsyncEventSourceClient*) {
    g_forceBroadcast.store(true, std::memory_order_release);
}

static size_t buildChayaPayload(int rx, int tx, bool connected, char* buf, size_t bufLen) {
    return static_cast<size_t>(
        snprintf(buf, bufLen, "{\"rx\":%d,\"tx\":%d,\"connected\":%s}", rx, tx,
                 connected ? "true" : "false"));
}

static size_t buildWifiStatusPayload(bool connected, const char* ssid, const char* ipStr, int rssi,
                                     char* buf, size_t bufLen) {
    if (!connected) {
        return static_cast<size_t>(snprintf(buf, bufLen, "{\"connected\":false}"));
    }
    size_t pos = 0;
    const int head =
        snprintf(buf, bufLen, "{\"connected\":true,\"ssid\":");
    if (head < 0 || static_cast<size_t>(head) >= bufLen) {
        return 0;
    }
    pos = static_cast<size_t>(head);
    if (!appendJsonStringQuotedEscaped(ssid != nullptr ? ssid : "", buf, bufLen, &pos)) {
        return 0;
    }
    const size_t remain = (bufLen > pos) ? (bufLen - pos) : 0;
    const int tail =
        snprintf(buf + pos, remain, ",\"ip\":\"%s\",\"rssi\":%d}", ipStr != nullptr ? ipStr : "", rssi);
    if (tail < 0 || static_cast<size_t>(tail) >= remain) {
        return 0;
    }
    return pos + static_cast<size_t>(tail);
}

static size_t buildMqttStatusPayload(bool connected, char* buf, size_t bufLen) {
    return static_cast<size_t>(
        snprintf(buf, bufLen, "{\"connected\":%s}", connected ? "true" : "false"));
}

} // namespace

void webEventsRegister(AsyncWebServer& ws) {
    g_events.authorizeConnect([](AsyncWebServerRequest* req) {
        if (configIsApMode()) {
            return true;
        }
        if (!configGetWebAuthEnabled()) {
            return true;
        }
        return webAuthIsAuthenticated(req);
    });
    g_events.onConnect(onEsConnect);
    ws.addHandler(&g_events);
}

void webEventsTick() {
    if (g_events.count() == 0) {
        return;
    }

    static unsigned s_wifiRfSkip     = 99U;
    static bool     s_cachedWifiConn = false;
    static char     s_cachedCurSsid[kWifiSsidMaxLen]{};
    static char     s_cachedCurIp[16]{};
    static int      s_cachedRssi     = 0;

    const int rx = heartDisplayRxDelta();
    const int tx = heartDisplayTxDelta();
    const bool mqttLineOk = mqttIsConnected();
    const bool mqttPageRelevant = mqttCfgIsBrokerConfigured();

    const bool force = g_forceBroadcast.load(std::memory_order_acquire);
    if (force) {
        s_wifiRfSkip = 99U;
    }

    bool chayaDirty =
        force || !g_haveLastChaya || g_lastRx != rx || g_lastTx != tx || g_lastMqttConn != mqttLineOk;

    bool     wifiConn = false;
    char     curSsid[kWifiSsidMaxLen]{};
    char     curIp[16]{};
    int      rssi = 0;
    if (s_wifiRfSkip >= 4U) {
        s_wifiRfSkip = 0U;
        wlanFillStaLinkSnapshot(&wifiConn, curIp, sizeof(curIp), curSsid, sizeof(curSsid), &rssi);
        s_cachedWifiConn = wifiConn;
        strlcpy(s_cachedCurSsid, curSsid, sizeof(s_cachedCurSsid));
        strlcpy(s_cachedCurIp, curIp, sizeof(s_cachedCurIp));
        s_cachedRssi = rssi;
    } else {
        ++s_wifiRfSkip;
        wifiConn = s_cachedWifiConn;
        strlcpy(curSsid, s_cachedCurSsid, sizeof(curSsid));
        strlcpy(curIp, s_cachedCurIp, sizeof(curIp));
        rssi = s_cachedRssi;
    }

    bool wifiDirty = false;
    {
        portENTER_CRITICAL(&g_esCacheMux);
        wifiDirty =
            force || !g_haveLastWifi || g_lastWifiConnected != wifiConn || g_lastWifiRssi != rssi
            || strcmp(g_lastWifiSsid, curSsid) != 0 || strcmp(g_lastWifiIp, curIp) != 0;
        if (wifiDirty) {
            g_haveLastWifi      = true;
            g_lastWifiConnected = wifiConn;
            strlcpy(g_lastWifiSsid, curSsid, sizeof(g_lastWifiSsid));
            strlcpy(g_lastWifiIp, curIp, sizeof(g_lastWifiIp));
            g_lastWifiRssi = rssi;
        }
        portEXIT_CRITICAL(&g_esCacheMux);
    }

    bool mqttStatusDirty = force;
    bool mqttConnNow     = false;
    if (mqttPageRelevant) {
        mqttConnNow = mqttIsConnected();
        if (!g_haveLastMqttStatus || g_lastMqttPageConn != mqttConnNow) {
            mqttStatusDirty = true;
        }
    } else {
        g_haveLastMqttStatus = false;
    }

    char buf[320];

    if (chayaDirty) {
        const size_t n = buildChayaPayload(rx, tx, mqttLineOk, buf, sizeof(buf));
        if (n > 0U && n < sizeof(buf)) {
            g_events.send(buf, "chaya");
        }
        g_haveLastChaya = true;
        g_lastRx        = rx;
        g_lastTx        = tx;
        g_lastMqttConn  = mqttLineOk;
    }

    if (wifiDirty) {
        size_t plen = 0;
        if (wifiConn) {
            plen = buildWifiStatusPayload(true, curSsid, curIp, rssi, buf, sizeof(buf));
        } else {
            plen = buildWifiStatusPayload(false, nullptr, nullptr, 0, buf, sizeof(buf));
        }
        if (plen > 0U && plen < sizeof(buf)) {
            g_events.send(buf, "wifi");
        }
    }

    if (mqttStatusDirty && mqttPageRelevant) {
        const size_t plen = buildMqttStatusPayload(mqttConnNow, buf, sizeof(buf));
        if (plen > 0U && plen < sizeof(buf)) {
            g_events.send(buf, "mqtt");
        }
        g_haveLastMqttStatus = true;
        g_lastMqttPageConn   = mqttConnNow;
    }

    g_forceBroadcast.store(false, std::memory_order_release);
}
