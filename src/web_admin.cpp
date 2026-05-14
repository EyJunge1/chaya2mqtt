#include "web_admin.h"

#include "config.h"
#include "display.h"
#include "mqtt.h"
#include "version.h"
#include "web_pages.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <esp_log.h>

#if defined(CORE_DEBUG_LEVEL) && CORE_DEBUG_LEVEL > 0
static const char* TAG = "WEB";
#else
static constexpr const char* TAG __attribute__((unused)) = "";
#endif

// ESP-IDF: Mozilla-CA-Bundle in libmbedtls (gleiche Nutzung wie mqtt.cpp).
extern const uint8_t x509_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t x509_crt_bundle_end[] asm("_binary_x509_crt_bundle_end");

AsyncWebServer& webAdminWebServer() {
    static AsyncWebServer server(80);
    return server;
}

static constexpr const char kGithubLatestReleaseApiUrl[] =
    "https://api.github.com/repos/EyJunge1/chaya2mqtt/releases/latest";

static constexpr const char kGithubLatestFirmwareBinUrl[] =
    "https://github.com/EyJunge1/chaya2mqtt/releases/latest/download/firmware.bin";

/** Mindestzeit (UTC Epoch) – ungefähr 2023-09, damit NTP bereits synchron wirkt */
static constexpr uint32_t kNtpMinValidUtcEpoch = 1700000000U;

static constexpr const char kCfgNamespace[]           = "cfg";
static constexpr const char kNvKeyUpdateCalendarDay[] = "upd_day";

static constexpr size_t kOtaUrlMax = 256;

static bool                      g_routesRegistered = false;
static std::atomic<bool>         g_rebootRequested{false};
static std::atomic<bool>         g_wifiConnectRequested{false};
static std::atomic<bool>         g_otaRequested{false};
static std::atomic<bool>         g_otaCheckRequested{false};
static std::atomic<bool>         g_mqttApplyPending{false};

/** Nur in `webAdminLoop()` (Arduino-Task) lesen nach Flag; Handler schreibt mit strlcpy. */
static char          g_otaUrl[kOtaUrlMax];
/** Aus Handler: neue MQTT-Konfiguration; Anwendung in `webAdminLoop()`. */
static MqttConfig    g_mqttPendingCfg;

/** NVS-Cache für letzten gespeicherten UTC-Kalendertag (`upd_day`), kein Lesen pro Loop. */
static uint32_t      s_cachedNvUpdateDay     = UINT32_MAX;
static bool          s_nvUpdateDayCacheValid = false;

/** GitHub Releases: `tag_name` aus Roh-JSON ohne Parser-Bibliothek */
static bool githubParseLatestTag(const char* json, char* tagOut, size_t tagLen) {
    constexpr const char kKey[] = "\"tag_name\"";
    const char*             key = strstr(json, kKey);
    if (key == nullptr) {
        return false;
    }
    const char* colon = strchr(key, ':');
    if (colon == nullptr) {
        return false;
    }
    const char* p = colon + 1;
    while (*p == ' ' || *p == '\t') {
        ++p;
    }
    if (*p != '\"') {
        return false;
    }
    ++p;
    size_t i = 0;
    while (*p != '\0' && *p != '\"' && (i + 1) < tagLen) {
        tagOut[i++] = *p++;
    }
    tagOut[i] = '\0';
    return i > 0;
}

static bool nvLoadLastUpdateCalendarDay(uint32_t* outDayUtc) {
    if (outDayUtc == nullptr) {
        return false;
    }
    Preferences prefs;
    if (!prefs.begin(kCfgNamespace, true)) {
        *outDayUtc = 0;
        return false;
    }
    *outDayUtc = prefs.getUInt(kNvKeyUpdateCalendarDay, 0);
    prefs.end();
    return true;
}

static void nvSaveLastUpdateCalendarDay(uint32_t dayUtc) {
    Preferences prefs;
    if (!prefs.begin(kCfgNamespace, false)) {
        ESP_LOGE(TAG, "NVS cfg: kann upd_day nicht schreiben");
        return;
    }
    prefs.putUInt(kNvKeyUpdateCalendarDay, dayUtc);
    prefs.end();
    s_cachedNvUpdateDay     = dayUtc;
    s_nvUpdateDayCacheValid = true;
}

static uint32_t calendarDaySinceEpochUtc(time_t utc) {
    if (utc < 0) {
        utc = 0;
    }
    return static_cast<uint32_t>(static_cast<uint64_t>(utc) / 86400ULL);
}

/** Prüfe GitHub `releases/latest`; setzt bei neuer Tag-Version OTA-Anforderung (download URL). */
static void checkGithubUpdate() {
    if (WiFi.status() != WL_CONNECTED || WiFi.localIP()[0] == 0) {
        ESP_LOGW(TAG, "GitHub Update: kein Stations-WLAN");
        return;
    }

    WiFiClientSecure tls;
    tls.setCACertBundle(x509_crt_bundle_start,
                        static_cast<size_t>(x509_crt_bundle_end - x509_crt_bundle_start));
    tls.setTimeout(45000);  /* ms Lesen/Schreiben (TLS/stream) */

    HTTPClient https;
    if (!https.begin(tls, kGithubLatestReleaseApiUrl)) {
        ESP_LOGE(TAG, "GitHub API: HTTPS begin fehlgeschlagen");
        return;
    }

    https.setConnectTimeout(20000);  /* ms */
    https.setTimeout(45000);         /* gesamtes Request-timeout */
    https.addHeader(F("User-Agent"), F("chaya2mqtt-esp32"));
    https.addHeader(F("Accept"), F("application/vnd.github+json"));

    const int httpCode = https.GET();
    if (httpCode != HTTP_CODE_OK) {
        ESP_LOGE(TAG, "GitHub API: HTTP-Fehler %d", httpCode);
        https.end();
        return;
    }

    constexpr size_t kJsonBuf = 8192;
    char               jsonBuf[kJsonBuf];
    auto&              stream = https.getStream();
    char               remoteTag[64];
    bool               parseOk = false;

    {
        size_t              len       = 0;
        const unsigned long deadlineMs = millis() + 45000;
        while (https.connected() && len + 1 < kJsonBuf && millis() < deadlineMs) {
            if (stream.available() <= 0) {
                if (!https.connected()) {
                    break;
                }
                delay(10);
                continue;
            }
            const int toRead =
                std::min(static_cast<int>(kJsonBuf - 1 - len), stream.available());
            if (toRead <= 0) {
                break;
            }
            const int n = stream.readBytes(jsonBuf + len, toRead);
            if (n <= 0) {
                break;
            }
            len += static_cast<size_t>(n);
            jsonBuf[len] = '\0';
            if (githubParseLatestTag(jsonBuf, remoteTag, sizeof(remoteTag))) {
                parseOk = true;
                break;
            }
        }
        if (!parseOk && len > 0) {
            parseOk = githubParseLatestTag(jsonBuf, remoteTag, sizeof(remoteTag));
        }
    }
    https.end();

    if (!parseOk) {
        ESP_LOGE(TAG, "GitHub: konnte tag_name nicht parsen");
        return;
    }

    ESP_LOGI(TAG, "GitHub latest=%s, lokal=%s", remoteTag, APP_VERSION);

    if (strcmp(remoteTag, APP_VERSION) != 0) {
        ESP_LOGI(TAG, "Firmware-Update: neue Version auf GitHub verfügbar");
        strlcpy(g_otaUrl, kGithubLatestFirmwareBinUrl, sizeof(g_otaUrl));
        g_otaRequested.store(true, std::memory_order_release);
    } else {
        ESP_LOGI(TAG, "Firmware ist aktuell");
    }
}

/** Tägliche Prüfung (Kalendertag UTC, NVS) sowie manuelle Anfrage `g_otaCheckRequested`. */
static void autoUpdateLoop() {
    if (configIsApMode()) {
        return;
    }
    const bool wlanStaOk =
        (WiFi.status() == WL_CONNECTED && WiFi.localIP()[0] != 0);
    if (!wlanStaOk) {
        return;
    }

    const time_t utcNow = time(nullptr);
    const uint32_t todayUtcDay = calendarDaySinceEpochUtc(utcNow > 0 ? utcNow : 0);

    if (g_otaCheckRequested.exchange(false, std::memory_order_acq_rel)) {
        const bool ntpOk = utcNow > static_cast<time_t>(kNtpMinValidUtcEpoch);
        if (!ntpOk) {
            ESP_LOGW(TAG, "Manual update check: Zeit noch nicht plausibel (NTP?), prüfe trotzdem GitHub …");
        }
        checkGithubUpdate();
        if (ntpOk) {
            nvSaveLastUpdateCalendarDay(todayUtcDay);
        }
        return;
    }

    /* Kein automatischer täglicher Check ohne Release-Tag im Flash (Sources bauen "dev"). */
    if (strcmp(APP_VERSION, "dev") == 0) {
        return;
    }
    if (utcNow <= static_cast<time_t>(kNtpMinValidUtcEpoch)) {
        return;
    }

    if (!s_nvUpdateDayCacheValid) {
        (void)nvLoadLastUpdateCalendarDay(&s_cachedNvUpdateDay);
        s_nvUpdateDayCacheValid = true;
    }
    if (s_cachedNvUpdateDay == todayUtcDay) {
        return;
    }

    checkGithubUpdate();
    nvSaveLastUpdateCalendarDay(todayUtcDay);
}


static bool parseBodyParam(AsyncWebServerRequest* req, const char* name, String* outStr) {
    if (req == nullptr || !req->hasParam(name, true)) {
        return false;
    }
    *outStr = req->getParam(name, true)->value();
    return true;
}

static void handleWifiConnectPost(AsyncWebServerRequest* req) {
    String ssid, password;
    if (!parseBodyParam(req, "ssid", &ssid) || ssid.length() == 0) {
        req->redirect(F("/wifi"));
        return;
    }
    (void)parseBodyParam(req, "password", &password);
    if (!configSaveWiFiCredentials(ssid.c_str(), password.c_str())) {
        req->redirect(F("/wifi"));
        return;
    }
    g_wifiConnectRequested.store(true, std::memory_order_release);
    streamSimpleDonePage(req, "Wi-Fi",
        "Wi-Fi gespeichert. Ger&auml;t startet neu.<br>"
        "MQTT und weitere Einstellungen unter "
        "<strong>http://chaya2mqtt.local</strong> konfigurieren (gleiches WLAN).");
}

static void handleUpdatePost(AsyncWebServerRequest* req) {
    String url;
    if (!parseBodyParam(req, "url", &url) || url.length() < 10) {
        req->redirect(F("/update"));
        return;
    }
    strlcpy(g_otaUrl, url.c_str(), sizeof(g_otaUrl));
    g_otaRequested.store(true, std::memory_order_release);
    streamSimpleDonePage(req, "Update", "Update starting…");
}

static void handleUpdateCheckPost(AsyncWebServerRequest* req) {
    g_otaCheckRequested.store(true, std::memory_order_release);
    streamSimpleDonePage(req, "Update", "Checking GitHub; an update may follow…");
}

static void handleRebootPost(AsyncWebServerRequest* req) {
    g_rebootRequested.store(true, std::memory_order_release);
    streamSimpleDonePage(req, "Reboot", "Rebooting…");
}

static void handleMqttPost(AsyncWebServerRequest* req) {
    MqttConfig pending = mqttCfg;

    if (req->hasParam("mqtt_server", true)) {
        strlcpy(pending.server, req->getParam("mqtt_server", true)->value().c_str(),
                sizeof(pending.server));
    }
    if (req->hasParam("mqtt_port", true)) {
        const int p = atoi(req->getParam("mqtt_port", true)->value().c_str());
        pending.port = (p > 0 && p <= 65535) ? static_cast<uint16_t>(p) : 8883;
    }
    if (req->hasParam("mqtt_user", true)) {
        strlcpy(pending.username, req->getParam("mqtt_user", true)->value().c_str(),
                sizeof(pending.username));
    }
    if (req->hasParam("mqtt_pass", true)) {
        strlcpy(pending.password, req->getParam("mqtt_pass", true)->value().c_str(),
                sizeof(pending.password));
    }
    if (req->hasParam("mqtt_topic_pub", true)) {
        strlcpy(pending.topicPub, req->getParam("mqtt_topic_pub", true)->value().c_str(),
                sizeof(pending.topicPub));
    }
    if (req->hasParam("mqtt_topic_sub", true)) {
        strlcpy(pending.topicSub, req->getParam("mqtt_topic_sub", true)->value().c_str(),
                sizeof(pending.topicSub));
    }
    if (strcmp(pending.topicPub, pending.topicSub) == 0) {
        ESP_LOGW(TAG, "MQTT: Pub/Sub-Topic identisch, setze Defaults");
        strlcpy(pending.topicPub, "heart/to_b", sizeof(pending.topicPub));
        strlcpy(pending.topicSub, "heart/to_a", sizeof(pending.topicSub));
    }
    g_mqttPendingCfg   = pending;
    g_mqttApplyPending.store(true, std::memory_order_release);
    req->redirect(F("/mqtt?saved=1"));
}

void webAdminRegisterRoutes() {
    if (g_routesRegistered) {
        return;
    }
    g_routesRegistered = true;

    AsyncWebServer& ws = webAdminWebServer();
    ws.on("/", HTTP_GET, [](AsyncWebServerRequest* rq) {
        streamDashboard(rq);
    });
    ws.on("/wifi", HTTP_GET, [](AsyncWebServerRequest* rq) {
        streamWifiPage(rq);
    });
    ws.on("/wifi-scan", HTTP_GET, [](AsyncWebServerRequest* rq) {
        handleWifiScanJson(rq);
    });
    ws.on("/wifi-connect", HTTP_POST, [](AsyncWebServerRequest* rq) {
        handleWifiConnectPost(rq);
    });
    ws.on("/update", HTTP_GET, [](AsyncWebServerRequest* rq) {
        if (configIsApMode()) { rq->redirect(F("/")); return; }
        streamUpdatePage(rq);
    });
    ws.on("/update", HTTP_POST, [](AsyncWebServerRequest* rq) {
        if (configIsApMode()) { rq->redirect(F("/")); return; }
        handleUpdatePost(rq);
    });
    ws.on("/update-check", HTTP_POST, [](AsyncWebServerRequest* rq) {
        if (configIsApMode()) { rq->redirect(F("/")); return; }
        handleUpdateCheckPost(rq);
    });
    ws.on("/reboot", HTTP_POST, [](AsyncWebServerRequest* rq) {
        if (configIsApMode()) { rq->redirect(F("/")); return; }
        handleRebootPost(rq);
    });

    ws.on("/mqtt", HTTP_GET, [](AsyncWebServerRequest* rq) {
        if (configIsApMode()) { rq->redirect(F("/")); return; }
        streamMqttHtmlPage(rq, rq->hasParam("saved"));
    });
    ws.on("/mqtt", HTTP_POST, [](AsyncWebServerRequest* rq) {
        if (configIsApMode()) { rq->redirect(F("/")); return; }
        handleMqttPost(rq);
    });

    ws.onNotFound([](AsyncWebServerRequest* rq) {
        rq->redirect(F("/"));
    });
}

void webAdminLoop() {
    if (g_mqttApplyPending.exchange(false, std::memory_order_acq_rel)) {
        mqttCfg = g_mqttPendingCfg;
        saveMQTTConfig();
        mqttDisconnect();
        mqttSetup();
        if (mqttCfg.server[0] != '\0') {
            requestHeartRedraw();
        }
    }

    if (g_rebootRequested.exchange(false, std::memory_order_acq_rel)
        || g_wifiConnectRequested.exchange(false, std::memory_order_acq_rel)) {
        flushHeartCounterIfDirty();
        delay(200);
        releaseGpioHoldBeforeRestart();
        ESP.restart();
    }

    autoUpdateLoop();

    if (g_otaRequested.exchange(false, std::memory_order_acq_rel)) {
        flushHeartCounterIfDirty();

        char urlCopy[kOtaUrlMax];
        strlcpy(urlCopy, g_otaUrl, sizeof(urlCopy));

        WiFiClientSecure client;
        client.setCACertBundle(x509_crt_bundle_start,
                               static_cast<size_t>(x509_crt_bundle_end - x509_crt_bundle_start));
        HTTPUpdate httpUpdate;
        httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
        const t_httpUpdate_return rc = httpUpdate.update(client, String(urlCopy));
        if (rc == HTTP_UPDATE_OK) {
            ESP_LOGI(TAG, "OTA ok, Neustart");
            flushHeartCounterIfDirty();
            delay(200);
            releaseGpioHoldBeforeRestart();
            ESP.restart();
        } else if (rc == HTTP_UPDATE_NO_UPDATES) {
            ESP_LOGW(TAG, "OTA: keine Updates");
        } else {
            ESP_LOGE(TAG, "OTA Fehler=%d (%s)",
                     static_cast<int>(httpUpdate.getLastError()), httpUpdate.getLastErrorString().c_str());
        }
    }
}
