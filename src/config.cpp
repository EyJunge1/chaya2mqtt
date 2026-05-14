#include "config.h"

#include "display.h"
#include "mqtt.h"
#include "web_admin.h"

#include <Arduino.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <time.h>

#if defined(CORE_DEBUG_LEVEL) && CORE_DEBUG_LEVEL > 0
static const char* TAG = "CFG";
#else
static constexpr const char* TAG __attribute__((unused)) = "";
#endif

// ─── Globale MQTT-Konfiguration ───────────────────────────────────────────────

MqttConfig mqttCfg;

// ─── Herz-Zähler ──────────────────────────────────────────────────────────────

int heartCounter     = 0;
int heartSentCounter = 0;

int counterBaseline     = 0;
int sentCountBaseline   = 0;

static uint32_t s_lastResetCalendarDayUtc = UINT32_MAX;

static constexpr uint32_t kNtpMinValidUtcEpoch = 1700000000U;

static constexpr const char kNvRstPeriod[] = "rstPeriod";

static int           lastCommittedHeartCounter               = 0;
static unsigned long lastHeartCounterSaveMs                  = 0;
static int           lastCommittedHeartSentCounter           = 0;
static unsigned long lastHeartSentCounterSaveMs               = 0;
static constexpr unsigned long kHeartCounterSaveMinIntervalMs = 30000;

// ─── WiFi / Captive Portal ────────────────────────────────────────────────────

static Preferences preferences;

static constexpr char kDeviceHostname[] = "chaya2mqtt";
static constexpr char kSetupApSsid[]    = "Chaya2MQTT";

static DNSServer      g_dnsServer;
static bool           g_apMode = false;

/** STA-Reconnect-Backoff (Event-Task): weniger Strom/Leerlauf-Reconnect-Wut bei dauerhaft fehlendem AP. */
static unsigned long       s_wifiReconnectNextAllowedMs = 0;
static uint32_t            s_wifiReconnectFailCount     = 0;
/** GOT_IP (WiFi-Event-Task): mDNS in configLoop() neu starten. */
static std::atomic<bool>   s_mdnsRestartNeeded{false};

static void wifiStationEvent(arduino_event_id_t event);

// ─── NVS-Hilfen WiFi ──────────────────────────────────────────────────────────

bool configSaveWiFiCredentials(const char* ssid, const char* password) {
    if (ssid == nullptr || ssid[0] == '\0') {
        return false;
    }
    if (!preferences.begin("wifi", false)) {
        ESP_LOGE(TAG, "NVS wifi: schreiben fehlgeschlagen (/wifi-connect)");
        return false;
    }
    preferences.putString("ssid", ssid);
    preferences.putString("pass", password != nullptr ? password : "");
    preferences.end();
    return true;
}

bool configIsApMode() {
    return g_apMode;
}

static void wifiStationEvent(arduino_event_id_t event) {
    if (g_apMode) {
        return;
    }
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
            const unsigned long nowMs = millis();
            if (nowMs < s_wifiReconnectNextAllowedMs) {
                ESP_LOGD(TAG, "WLAN reconnect übersprungen (Backoff)");
                break;
            }
            ESP_LOGW(TAG, "WLAN getrennt, versuche Reconnect...");
            if (WiFi.getMode() == WIFI_STA || WiFi.getMode() == WIFI_AP_STA) {
                WiFi.reconnect();
                constexpr unsigned long kBaseBackoffMs = 3000UL;
                constexpr unsigned long kMaxBackoffMs  = 120000UL;
                const uint32_t          shift =
                    std::min(s_wifiReconnectFailCount, static_cast<uint32_t>(6));
                const unsigned long backoff =
                    std::min(kBaseBackoffMs * (1UL << shift), kMaxBackoffMs);
                s_wifiReconnectFailCount++;
                s_wifiReconnectNextAllowedMs = nowMs + backoff;
            }
            break;
        }
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            s_wifiReconnectFailCount     = 0;
            s_wifiReconnectNextAllowedMs = 0;
            ESP_LOGI(TAG, "WLAN Sta-IP: %s", WiFi.localIP().toString().c_str());
            s_mdnsRestartNeeded.store(true, std::memory_order_release);
            break;
        default:
            break;
    }
}

// ─── NVS: MQTT ────────────────────────────────────────────────────────────────

void loadMQTTConfig() {
    if (!preferences.begin("mqtt", true)) {
        ESP_LOGW(TAG, "NVS mqtt: lesen fehlgeschlagen, nutze Defaults");
        strlcpy(mqttCfg.topicPub, "chaya/to_b", sizeof(mqttCfg.topicPub));
        strlcpy(mqttCfg.topicSub, "chaya/to_a", sizeof(mqttCfg.topicSub));
        return;
    }
    if (!preferences.isKey("server")) {
        preferences.end();
        ESP_LOGI(TAG, "MQTT noch nicht konfiguriert, nutze Defaults");
        strlcpy(mqttCfg.topicPub, "chaya/to_b", sizeof(mqttCfg.topicPub));
        strlcpy(mqttCfg.topicSub, "chaya/to_a", sizeof(mqttCfg.topicSub));
        return;
    }
    preferences.getString("server", mqttCfg.server, sizeof(mqttCfg.server));
    const int p = preferences.getInt("port", 8883);
    mqttCfg.port = (p > 0 && p <= 65535) ? static_cast<uint16_t>(p) : 8883;
    preferences.getString("user", mqttCfg.username, sizeof(mqttCfg.username));
    preferences.getString("pass", mqttCfg.password, sizeof(mqttCfg.password));
    if (preferences.getString("topic_pub", mqttCfg.topicPub, sizeof(mqttCfg.topicPub)) == 0
        || mqttCfg.topicPub[0] == '\0') {
        strlcpy(mqttCfg.topicPub, "chaya/to_b", sizeof(mqttCfg.topicPub));
    }
    if (preferences.getString("topic_sub", mqttCfg.topicSub, sizeof(mqttCfg.topicSub)) == 0
        || mqttCfg.topicSub[0] == '\0') {
        strlcpy(mqttCfg.topicSub, "chaya/to_a", sizeof(mqttCfg.topicSub));
    }
    preferences.end();
}

void saveMQTTConfig() {
    if (!preferences.begin("mqtt", false)) {
        ESP_LOGE(TAG, "NVS mqtt: schreiben fehlgeschlagen");
        return;
    }
    preferences.putString("server", mqttCfg.server);
    preferences.putInt("port", mqttCfg.port);
    preferences.putString("user", mqttCfg.username);
    preferences.putString("pass", mqttCfg.password);
    preferences.putString("topic_pub", mqttCfg.topicPub);
    preferences.putString("topic_sub", mqttCfg.topicSub);
    preferences.end();
}

// ─── Kalendertag / Anzeige-Baseline ───────────────────────────────────────────

uint32_t calendarDaySinceEpochUtc(time_t utc) {
    if (utc < 0) {
        utc = 0;
    }
    return static_cast<uint32_t>(static_cast<uint64_t>(utc) / 86400ULL);
}

void loadCounterBaseline() {
    if (!preferences.begin("chaya", true)) {
        ESP_LOGW(TAG, "NVS chaya: Baseline lesen fehlgeschlagen, = 0");
        counterBaseline            = 0;
        sentCountBaseline          = 0;
        s_lastResetCalendarDayUtc  = UINT32_MAX;
        return;
    }
    counterBaseline           = std::max<int32_t>(preferences.getInt("cntBase", 0), 0);
    sentCountBaseline         = std::max<int32_t>(preferences.getInt("sntBase", 0), 0);
    s_lastResetCalendarDayUtc = preferences.getUInt("rstDay", UINT32_MAX);
    preferences.end();
}

bool configGetResetPeriodIsWeekly() {
    Preferences prefs;
    if (!prefs.begin("cfg", true)) {
        return false;
    }
    const bool weekly = (prefs.getUChar(kNvRstPeriod, 0) != 0);
    prefs.end();
    return weekly;
}

void configSetResetPeriodWeekly(bool weekly) {
    Preferences prefs;
    if (!prefs.begin("cfg", false)) {
        ESP_LOGE(TAG, "NVS cfg: rstPeriod schreiben fehlgeschlagen");
        return;
    }
    prefs.putUChar(kNvRstPeriod, weekly ? 1 : 0);
    prefs.end();
}

/** Persist baseline + last reset day after periodic roll or first NTP anchor. */
static bool persistCounterBaselineState() {
    if (!preferences.begin("chaya", false)) {
        ESP_LOGE(TAG, "NVS chaya: Baseline schreiben fehlgeschlagen");
        return false;
    }
    preferences.putInt("cntBase", counterBaseline);
    preferences.putInt("sntBase", sentCountBaseline);
    preferences.putUInt("rstDay", s_lastResetCalendarDayUtc);
    preferences.end();
    return true;
}

void maybePeriodicallyResetCounters() {
    if (g_apMode) {
        return;
    }
    const time_t utcNow = time(nullptr);
    if (utcNow <= static_cast<time_t>(kNtpMinValidUtcEpoch)) {
        return;
    }
    const uint32_t currentDay = calendarDaySinceEpochUtc(utcNow);

    if (s_lastResetCalendarDayUtc == UINT32_MAX) {
        s_lastResetCalendarDayUtc = currentDay;
        if (!persistCounterBaselineState()) {
            s_lastResetCalendarDayUtc = UINT32_MAX;
        }
        return;
    }

    const bool weekly = configGetResetPeriodIsWeekly();
    bool        shouldReset = false;
    if (weekly) {
        /* Calendar day index is monotonic for device lifetime. */
        shouldReset = (currentDay >= s_lastResetCalendarDayUtc + 7U);
    } else {
        shouldReset = (currentDay != s_lastResetCalendarDayUtc);
    }

    if (!shouldReset) {
        return;
    }

    counterBaseline  = heartCounter;
    sentCountBaseline = heartSentCounter;
    s_lastResetCalendarDayUtc = currentDay;
    if (persistCounterBaselineState()) {
        ESP_LOGI(TAG, "Periodic display counter reset (%s)", weekly ? "weekly" : "daily");
        requestHeartRedraw();
    }
}

// ─── NVS: Herz-Zähler ─────────────────────────────────────────────────────────

void loadHeartCounter() {
    if (!preferences.begin("chaya", true)) {
        ESP_LOGW(TAG, "NVS chaya: lesen fehlgeschlagen, Zaehler = 0");
        heartCounter                 = 0;
        heartSentCounter             = 0;
        lastCommittedHeartCounter    = 0;
        lastCommittedHeartSentCounter = 0;
        lastHeartCounterSaveMs       = millis();
        lastHeartSentCounterSaveMs   = millis();
        loadCounterBaseline();
        return;
    }
    heartCounter     = std::max<int32_t>(preferences.getInt("counter", 0), 0);
    heartSentCounter = std::max<int32_t>(preferences.getInt("sentCount", 0), 0);
    preferences.end();
    lastCommittedHeartCounter     = heartCounter;
    lastCommittedHeartSentCounter = heartSentCounter;
    lastHeartCounterSaveMs        = millis();
    lastHeartSentCounterSaveMs    = millis();
    loadCounterBaseline();
}

void loadHeartSentCounter() {
    if (!preferences.begin("chaya", true)) {
        ESP_LOGW(TAG, "NVS chaya: lesen sentCount fehlgeschlagen, = 0");
        heartSentCounter             = 0;
        lastCommittedHeartSentCounter = heartSentCounter;
        lastHeartSentCounterSaveMs   = millis();
        return;
    }
    heartSentCounter = std::max<int32_t>(preferences.getInt("sentCount", 0), 0);
    preferences.end();
    lastCommittedHeartSentCounter = heartSentCounter;
    lastHeartSentCounterSaveMs    = millis();
}

bool saveHeartCounter() {
    if (!preferences.begin("chaya", false)) {
        ESP_LOGE(TAG, "NVS chaya: schreiben fehlgeschlagen");
        return false;
    }
    preferences.putInt("counter", heartCounter);
    preferences.end();
    return true;
}

bool saveHeartSentCounter() {
    if (!preferences.begin("chaya", false)) {
        ESP_LOGE(TAG, "NVS chaya: schreiben sentCount fehlgeschlagen");
        return false;
    }
    preferences.putInt("sentCount", heartSentCounter);
    preferences.end();
    return true;
}

void maybeSaveHeartCounter() {
    if (heartCounter == lastCommittedHeartCounter) {
        return;
    }
    const unsigned long now = millis();
    if (now - lastHeartCounterSaveMs >= kHeartCounterSaveMinIntervalMs) {
        if (saveHeartCounter()) {
            lastCommittedHeartCounter = heartCounter;
            lastHeartCounterSaveMs    = now;
        }
    }
}

void maybeSaveHeartSentCounter() {
    if (heartSentCounter == lastCommittedHeartSentCounter) {
        return;
    }
    const unsigned long now = millis();
    if (now - lastHeartSentCounterSaveMs >= kHeartCounterSaveMinIntervalMs) {
        if (saveHeartSentCounter()) {
            lastCommittedHeartSentCounter = heartSentCounter;
            lastHeartSentCounterSaveMs    = now;
        }
    }
}

void flushHeartCounterIfDirty() {
    if (heartCounter != lastCommittedHeartCounter) {
        if (saveHeartCounter()) {
            lastCommittedHeartCounter = heartCounter;
            lastHeartCounterSaveMs    = millis();
        }
    }
}

void flushHeartSentCounterIfDirty() {
    if (heartSentCounter != lastCommittedHeartSentCounter) {
        if (saveHeartSentCounter()) {
            lastCommittedHeartSentCounter = heartSentCounter;
            lastHeartSentCounterSaveMs    = millis();
        }
    }
}

// ─── WiFi-Setup ───────────────────────────────────────────────────────────────

void setupWiFi() {
    webAdminRegisterRoutes();

    String ssid, pass;
    if (preferences.begin("wifi", true)) {
        ssid = preferences.getString("ssid", "");
        pass = preferences.getString("pass", "");
        preferences.end();
    }

    // Event-Handler erst nach setup() registrieren – nicht während des Verbindungsversuchs,
    // da ansonsten DISCONNECTED-Events während WiFi.begin() WiFi.reconnect() auslösen.
    bool staConnected = false;

    if (ssid.length() > 0) {
        WiFi.mode(WIFI_STA);
        // DHCP + Hostname: ohne config() vor setHostname() ignoriert der Core den Option-12-Namen (Bug #2537).
        WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
        WiFi.setHostname(kDeviceHostname);
        WiFi.persistent(false);
        WiFi.setAutoReconnect(false);
        WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
        WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
        WiFi.begin(ssid.c_str(), pass.c_str());

        const unsigned long start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
            delay(100);
        }
        staConnected = (WiFi.status() == WL_CONNECTED && WiFi.localIP()[0] != 0);
    }

    if (staConnected) {
        WiFi.setSleep(true);
        // NTP: für tägliche Auto-Updates (Kalendertag) und Zeitvergleiche
        configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
        esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
        (void)esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20);
        esp_wifi_set_max_tx_power(52);
        if (!MDNS.begin(kDeviceHostname)) {
            ESP_LOGW(TAG, "mDNS.begin fehlgeschlagen");
        }
        MDNS.addService("http", "tcp", 80);
        // Jetzt erst Event-Handler registrieren (Reconnect bei Disconnect im Betrieb).
        WiFi.onEvent(wifiStationEvent);
        ESP_LOGI(TAG, "WLAN STA bereit (%s / %s)", kDeviceHostname, WiFi.localIP().toString().c_str());
    } else {
        // g_apMode ZUERST setzen, damit der (noch nicht registrierte) Event-Handler
        // kein WiFi.reconnect() auslöst falls noch alte Events pending sind.
        g_apMode = true;

        // Komplett neu starten: WIFI_OFF -> WIFI_AP, kein disconnect(wifioff=true)
        // da das esp_wifi_stop() aufruft und softAPIP() dann 0.0.0.0 liefert.
        WiFi.mode(WIFI_OFF);
        delay(100);
        WiFi.softAPConfig(IPAddress(4, 3, 2, 1), IPAddress(4, 3, 2, 1), IPAddress(255, 255, 255, 0));
        WiFi.mode(WIFI_AP);
        WiFi.softAP(kSetupApSsid);
        delay(100);  // warten bis AP_STARTED Event intern verarbeitet ist
        g_dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
        g_dnsServer.start(53, "*", WiFi.softAPIP());
        ESP_LOGI(TAG, "WLAN AP: %s, IP %s", kSetupApSsid, WiFi.softAPIP().toString().c_str());
    }

    webAdminWebServer().begin();
}

// ─── Factory Reset ────────────────────────────────────────────────────────────

/** Display CS (15) und Button-LED (4) nutzen gpio_hold_en fuer Light-Sleep. */
void releaseGpioHoldBeforeRestart() {
    (void)gpio_hold_dis(GPIO_NUM_15);
    (void)gpio_hold_dis(GPIO_NUM_4);
}

void resetAllSettings() {
    ESP_LOGW(TAG, "Factory Reset: alle Einstellungen loeschen...");
    webAdminWebServer().end();
    if (!g_apMode) {
        MDNS.end();
    }
    WiFi.softAPdisconnect(true);
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_MODE_NULL);

    if (preferences.begin("wifi", false)) {
        preferences.clear();
        preferences.end();
    }
    if (preferences.begin("mqtt", false)) {
        preferences.clear();
        preferences.end();
    }
    if (preferences.begin("cfg", false)) {
        preferences.clear();
        preferences.end();
    }
    if (preferences.begin("chaya", false)) {
        preferences.clear();
        preferences.end();
    }
    /* Kein flushHeartCounterIfDirty: wuerde chaya in NVS nach clear wieder anlegen. */
    heartCounter                  = 0;
    heartSentCounter              = 0;
    counterBaseline               = 0;
    sentCountBaseline             = 0;
    s_lastResetCalendarDayUtc     = UINT32_MAX;
    lastCommittedHeartCounter     = 0;
    lastCommittedHeartSentCounter = 0;
    lastHeartCounterSaveMs        = millis();
    lastHeartSentCounterSaveMs    = millis();
    delay(500);
    releaseGpioHoldBeforeRestart();
    ESP.restart();
}

// ─── Loop ─────────────────────────────────────────────────────────────────────

void configLoop() {
    if (g_apMode) {
        g_dnsServer.processNextRequest();
    }
    if (s_mdnsRestartNeeded.exchange(false, std::memory_order_acq_rel) && !g_apMode) {
        MDNS.end();
        if (!MDNS.begin(kDeviceHostname)) {
            ESP_LOGW(TAG, "mDNS.begin nach GOT_IP fehlgeschlagen");
        }
        MDNS.addService("http", "tcp", 80);
    }
    webAdminLoop();
    if (!g_apMode) {
        maybePeriodicallyResetCounters();
    }
}

bool configIsSetupPortalActive() {
    return configIsApMode();
}
