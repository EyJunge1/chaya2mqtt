#include "web_admin.h"

#include "config.h"
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
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <esp_log.h>

#if defined(CORE_DEBUG_LEVEL) && CORE_DEBUG_LEVEL > 0
static const char* TAG = "WEB";
#else
static constexpr const char* TAG __attribute__((unused)) = "";
#endif

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

static bool            g_routesRegistered        = false;
static bool            g_rebootRequested         = false;
static bool            g_wifiConnectRequested    = false;
static bool            g_otaRequested            = false;
static bool            g_otaCheckRequested       = false;
static String          g_otaUrl;

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
    tls.setInsecure();
    tls.setTimeout(45000);  /* ms Lesen/schreiben (TLS/stream) */

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

    const String payload = https.getString();
    https.end();

    char remoteTag[64];
    if (!githubParseLatestTag(payload.c_str(), remoteTag, sizeof(remoteTag))) {
        ESP_LOGE(TAG, "GitHub: konnte tag_name nicht parsen");
        return;
    }

    ESP_LOGI(TAG, "GitHub latest=%s, lokal=%s", remoteTag, APP_VERSION);

    if (strcmp(remoteTag, APP_VERSION) != 0) {
        ESP_LOGI(TAG, "Firmware-Update: neue Version auf GitHub verfügbar");
        g_otaUrl       = kGithubLatestFirmwareBinUrl;
        g_otaRequested = true;
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

    if (g_otaCheckRequested) {
        g_otaCheckRequested = false;
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

    uint32_t lastDay = 0;
    (void)nvLoadLastUpdateCalendarDay(&lastDay);
    if (lastDay == todayUtcDay) {
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
    g_wifiConnectRequested = true;
    streamSimpleDonePage(req, "WLAN", "WLAN wurde gespeichert. Das Gerät startet neu …");
}

static void handleUpdatePost(AsyncWebServerRequest* req) {
    String url;
    if (!parseBodyParam(req, "url", &url) || url.length() < 10) {
        req->redirect(F("/update"));
        return;
    }
    g_otaUrl       = url;
    g_otaRequested = true;
    streamSimpleDonePage(req, "Update", "Update wird gestartet …");
}

static void handleUpdateCheckPost(AsyncWebServerRequest* req) {
    g_otaCheckRequested = true;
    streamSimpleDonePage(req, "Update", "Es wird gegen GitHub geprüft; ggf. startet dann ein Update …");
}

static void handleRebootPost(AsyncWebServerRequest* req) {
    g_rebootRequested = true;
    streamSimpleDonePage(req, "Neustart", "Neustart …");
}

static void handleMqttPost(AsyncWebServerRequest* req) {
    if (req->hasParam("mqtt_server", true)) {
        strlcpy(mqttCfg.server, req->getParam("mqtt_server", true)->value().c_str(),
                sizeof(mqttCfg.server));
    }
    if (req->hasParam("mqtt_port", true)) {
        const int p = atoi(req->getParam("mqtt_port", true)->value().c_str());
        mqttCfg.port = (p > 0 && p <= 65535) ? static_cast<uint16_t>(p) : 8883;
    }
    if (req->hasParam("mqtt_user", true)) {
        strlcpy(mqttCfg.username, req->getParam("mqtt_user", true)->value().c_str(),
                sizeof(mqttCfg.username));
    }
    if (req->hasParam("mqtt_pass", true)) {
        strlcpy(mqttCfg.password, req->getParam("mqtt_pass", true)->value().c_str(),
                sizeof(mqttCfg.password));
    }
    if (req->hasParam("mqtt_topic_pub", true)) {
        strlcpy(mqttCfg.topicPub, req->getParam("mqtt_topic_pub", true)->value().c_str(),
                sizeof(mqttCfg.topicPub));
    }
    if (req->hasParam("mqtt_topic_sub", true)) {
        strlcpy(mqttCfg.topicSub, req->getParam("mqtt_topic_sub", true)->value().c_str(),
                sizeof(mqttCfg.topicSub));
    }
    if (strcmp(mqttCfg.topicPub, mqttCfg.topicSub) == 0) {
        ESP_LOGW(TAG, "MQTT: Pub/Sub-Topic identisch, setze Defaults");
        strlcpy(mqttCfg.topicPub, "heart/to_b", sizeof(mqttCfg.topicPub));
        strlcpy(mqttCfg.topicSub, "heart/to_a", sizeof(mqttCfg.topicSub));
    }
    saveMQTTConfig();
    mqttDisconnect();
    mqttSetup();
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
        streamUpdatePage(rq);
    });
    ws.on("/update", HTTP_POST, [](AsyncWebServerRequest* rq) {
        handleUpdatePost(rq);
    });
    ws.on("/update-check", HTTP_POST, [](AsyncWebServerRequest* rq) {
        handleUpdateCheckPost(rq);
    });
    ws.on("/reboot", HTTP_POST, [](AsyncWebServerRequest* rq) {
        handleRebootPost(rq);
    });

    ws.on("/mqtt", HTTP_GET, [](AsyncWebServerRequest* rq) {
        streamMqttHtmlPage(rq, rq->hasParam("saved"));
    });
    ws.on("/mqtt", HTTP_POST, handleMqttPost);

    ws.onNotFound([](AsyncWebServerRequest* rq) {
        rq->redirect(F("/"));
    });
}

void webAdminLoop() {
    if (g_rebootRequested) {
        delay(200);
        ESP.restart();
    }
    if (g_wifiConnectRequested) {
        delay(200);
        ESP.restart();
    }
    autoUpdateLoop();

    if (g_otaRequested) {
        g_otaRequested = false;
        WiFiClientSecure client;
        client.setInsecure();
        HTTPUpdate httpUpdate;
        httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
        const t_httpUpdate_return rc = httpUpdate.update(client, g_otaUrl);
        if (rc == HTTP_UPDATE_OK) {
            ESP_LOGI(TAG, "OTA ok, Neustart");
            delay(200);
            ESP.restart();
        } else if (rc == HTTP_UPDATE_NO_UPDATES) {
            ESP_LOGW(TAG, "OTA: keine Updates");
        } else {
            ESP_LOGE(TAG, "OTA Fehler=%d (%s)",
                     static_cast<int>(httpUpdate.getLastError()), httpUpdate.getLastErrorString().c_str());
        }
    }
}
