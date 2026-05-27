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
#include <esp_log.h>

#include <atomic>
#include <climits>
#include <cstdio>
#include <cstring>

#include "log_tag.h"

DEFINE_LOG_TAG("SSE");

// Release builds strip ESP_LOG*; keep TAG referenced for -Wunused-variable.
static void sseLogTagAnchor() {
    (void)TAG;
}

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
bool     s_haveLastWifi       = false;
bool     s_lastWifiConnected  = false;
char     s_lastWifiSsid[kWifiSsidMaxLen]{};
char     s_lastWifiIp[16]{};
int      s_lastWifiRssi       = 0;
bool     s_haveLastMqttStatus = false;
bool     s_lastMqttPageConn   = false;

portMUX_TYPE s_esCacheMux = portMUX_INITIALIZER_UNLOCKED;

static void onEsConnect(AsyncEventSourceClient*) {
    if (!s_loggedFirstSseClient.exchange(true, std::memory_order_acq_rel)) {
        ESP_LOGD(TAG, "SSE: first client connected");
    }
    s_forceBroadcast.store(true, std::memory_order_release);
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
    s_events.authorizeConnect([](AsyncWebServerRequest* req) {
        if (s_events.count() >= kMaxSseClients) {
            return false;
        }
        if (configIsApMode()) {
            return true;
        }
        if (!configGetWebAuthEnabled()) {
            return true;
        }
        return webAuthIsAuthenticated(req);
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
    static int      s_cachedRssi     = 0;

    const int rx = heartDisplayRxDelta();
    const int tx = heartDisplayTxDelta();
    const bool mqttLineOk = mqttIsConnected();
    const bool mqttPageRelevant = mqttCfgIsBrokerConfigured();

    const bool force = s_forceBroadcast.load(std::memory_order_acquire);
    if (force) {
        s_wifiRfSkip = 99U;
    }

    bool chayaDirty = false;
    {
        portENTER_CRITICAL(&s_esCacheMux);
        chayaDirty =
            force || !s_haveLastChaya || s_lastRx != rx || s_lastTx != tx || s_lastMqttConn != mqttLineOk;
        if (chayaDirty) {
            s_haveLastChaya = true;
            s_lastRx        = rx;
            s_lastTx        = tx;
            s_lastMqttConn  = mqttLineOk;
        }
        portEXIT_CRITICAL(&s_esCacheMux);
    }

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
        portENTER_CRITICAL(&s_esCacheMux);
        wifiDirty =
            force || !s_haveLastWifi || s_lastWifiConnected != wifiConn || s_lastWifiRssi != rssi
            || strcmp(s_lastWifiSsid, curSsid) != 0 || strcmp(s_lastWifiIp, curIp) != 0;
        if (wifiDirty) {
            s_haveLastWifi      = true;
            s_lastWifiConnected = wifiConn;
            strlcpy(s_lastWifiSsid, curSsid, sizeof(s_lastWifiSsid));
            strlcpy(s_lastWifiIp, curIp, sizeof(s_lastWifiIp));
            s_lastWifiRssi = rssi;
        }
        portEXIT_CRITICAL(&s_esCacheMux);
    }

    bool mqttStatusDirty = force;
    bool mqttConnNow     = false;
    if (mqttPageRelevant) {
        mqttConnNow = mqttIsConnected();
        portENTER_CRITICAL(&s_esCacheMux);
        if (!s_haveLastMqttStatus || s_lastMqttPageConn != mqttConnNow) {
            mqttStatusDirty = true;
        }
        if (mqttStatusDirty) {
            s_haveLastMqttStatus = true;
            s_lastMqttPageConn   = mqttConnNow;
        }
        portEXIT_CRITICAL(&s_esCacheMux);
    } else {
        portENTER_CRITICAL(&s_esCacheMux);
        s_haveLastMqttStatus = false;
        portEXIT_CRITICAL(&s_esCacheMux);
    }

    char buf[320];

    if (chayaDirty) {
        const size_t n = buildChayaPayload(rx, tx, mqttLineOk, buf, sizeof(buf));
        if (n > 0U && n < sizeof(buf)) {
            s_events.send(buf, "chaya");
        }
    }

    if (wifiDirty) {
        size_t plen = 0;
        if (wifiConn) {
            plen = buildWifiStatusPayload(true, curSsid, curIp, rssi, buf, sizeof(buf));
        } else {
            plen = buildWifiStatusPayload(false, nullptr, nullptr, 0, buf, sizeof(buf));
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

    s_forceBroadcast.store(false, std::memory_order_release);
}
