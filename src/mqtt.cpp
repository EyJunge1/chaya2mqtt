#include <Arduino.h>

#include "mqtt.h"

#include "mqtt_config.h"
#include "tls_bundle.h"
#include "counter.h"
#include "display.h"
#include "wlan.h"
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <algorithm>
#include <climits>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <esp_log.h>
#include <esp_random.h>
#include <esp32-hal-cpu.h>

#if defined(CORE_DEBUG_LEVEL) && CORE_DEBUG_LEVEL > 0
static const char* TAG = "MQTT";
#else
static constexpr const char* TAG __attribute__((unused)) = "";
#endif

static WiFiClientSecure espClient;
static PubSubClient client(espClient);

static constexpr unsigned long kMqttBackoffInitialMs = 5000;
static constexpr unsigned long kMqttBackoffMaxMs = 60000;
/** Longer wait when STA is down so reconnect/logging is not spammed. */
static constexpr unsigned long kMqttWifiDownBackoffMs = 20000UL;

static unsigned long lastMqttAttemptAt = 0;
static unsigned long mqttBackoffMs = 0;
static unsigned long mqttCurrentBackoffMs = kMqttBackoffInitialMs;

/** After boot, wait briefly for SNTP before TLS (avoids MBEDTLS_ERR_X509_CERT_VERIFY_FAILED at epoch). */
static constexpr unsigned long kMqttNtpStartupGuardMs = 30000UL;
static constexpr unsigned long kMqttNtpRetryMs        = 2000UL;

static constexpr uint32_t kMqttTlsBoostCpuMhz = 240;

/** Eine Verbindungsrunde; Rückgabe: Millisekunden bis zum nächsten Versuch. */
static unsigned long mqttTryConnectSinglePass() {
    MqttConfig cfg{};
    mqttCfgSnapshot(&cfg);

    if (strlen(cfg.server) == 0) {
        ESP_LOGW(TAG, "Kein MQTT-Server konfiguriert. Wartungs-AP nutzen oder /mqtt im Einrichtungs-WLAN");
        return 60000;
    }

    if (!wlanStaConnectedOk()) {
        ESP_LOGD(TAG, "WiFi not connected, deferring MQTT attempt");
        return kMqttWifiDownBackoffMs;
    }
    if (!wlanStaStableForMqtt()) {
        ESP_LOGD(TAG, "WiFi not stable yet after GOT_IP, deferring MQTT attempt");
        return kMqttNtpRetryMs;
    }

    char clientId[24];
    snprintf(clientId, sizeof(clientId), "Chaya2MQTT-%04lX",
             static_cast<unsigned long>(esp_random() & 0xffffU));

    ESP_LOGI(TAG, "Verbinde mit MQTT (TLS)... Server: %s:%u, Client: %s",
             cfg.server, cfg.port, clientId);

    if (millis() < kMqttNtpStartupGuardMs && !wlanNtpSynced()) {
        ESP_LOGI(TAG, "Warte auf NTP vor MQTT/TLS (%lu ms)", kMqttNtpRetryMs);
        return kMqttNtpRetryMs;
    }

    char willTopic[140];
    snprintf(willTopic, sizeof(willTopic), "%s/lwt", cfg.topicPub);

    if (!wlanStaConnectedOk()) {
        setCpuFrequencyMhz(80);
        return kMqttWifiDownBackoffMs;
    }

    setCpuFrequencyMhz(static_cast<int>(kMqttTlsBoostCpuMhz));
    const bool connected =
        client.connect(clientId, cfg.username, cfg.password, willTopic, 1, true, "offline", true);
    setCpuFrequencyMhz(80);

    if (connected) {
        ESP_LOGI(TAG, "Verbunden! Subscribing zu Topic (QoS 1): %s", cfg.topicSub);
        (void)client.publish(willTopic, "online", true);
        mqttCurrentBackoffMs = kMqttBackoffInitialMs;
        if (!client.subscribe(cfg.topicSub, 1)) {
            ESP_LOGE(TAG, "Subscribe fehlgeschlagen, disconnect fuer Retry");
            client.disconnect();
            const unsigned long waitMs = mqttCurrentBackoffMs;
            mqttCurrentBackoffMs = std::min(mqttCurrentBackoffMs * 2UL, kMqttBackoffMaxMs);
            ESP_LOGI(TAG, "Naechster Versuch in %lu s (Subscribe-Fehler)", waitMs / 1000UL);
            return waitMs;
        }
        return 0;
    }

    ESP_LOGE(TAG, "Verbindung fehlgeschlagen, rc=%d", client.state());
    const unsigned long waitMs = mqttCurrentBackoffMs;
    mqttCurrentBackoffMs = std::min(mqttCurrentBackoffMs * 2UL, kMqttBackoffMaxMs);
    ESP_LOGI(TAG, "Naechster Versuch in %lu s (exponentieller Backoff)", waitMs / 1000UL);
    return waitMs;
}

// NOLINTNEXTLINE(readability-non-const-parameter) - PubSubClient callback signature is fixed
static void mqttCallback(char* topic, byte* payload, unsigned int length) {
    MqttConfig cfg{};
    mqttCfgSnapshot(&cfg);
    if (strcmp(topic, cfg.topicSub) != 0) {
        ESP_LOGD(TAG, "Payload ignoriert (Topic != topicSub)");
        return;
    }
    if (length == 0 || length > 10U) {
        ESP_LOGD(TAG, "Ungueltiger Zaehler-Payload (len=%u)", length);
        return;
    }

    char buf[16];
    memcpy(buf, payload, length);
    buf[length] = '\0';

    char* endPtr = nullptr;
    errno = 0;
    const long parsed = strtol(buf, &endPtr, 10);
    /* On ESP32, long == int; ERANGE still catches overflow from strtol. */
    if (errno == ERANGE || endPtr != buf + length || parsed < 0 || parsed > static_cast<long>(INT_MAX)) {
        ESP_LOGD(TAG, "Zaehler-Payload nicht vollstaendig gueltige Zahl");
        return;
    }

    const int newCounter = static_cast<int>(parsed);
    if (newCounter == heartCounter) {
        return;
    }

    ESP_LOGI(TAG, "Zaehler empfangen (remote gesendete Herzen): %d", newCounter);
    heartCounter = newCounter;
    requestHeartRedraw();
}

bool mqttPublishChaya() {
    if (!client.connected()) {
        ESP_LOGW(TAG, "Publish fehlgeschlagen: nicht verbunden");
        return false;
    }
    MqttConfig cfg{};
    mqttCfgSnapshot(&cfg);

    char buf[16];
    const long nextVal = static_cast<long>(heartSentCounter) + 1L;
    if (nextVal > INT_MAX) {
        ESP_LOGW(TAG, "Publish uebersprungen: heartSentCounter hat Maximum");
        return false;
    }
    static_cast<void>(snprintf(buf, sizeof(buf), "%ld", nextVal));
    return client.publish(cfg.topicPub, buf, true);
}

void mqttDisconnect() {
    client.disconnect();
}

void mqttSetup() {
    MqttConfig cfg{};
    mqttCfgSnapshot(&cfg);

    espClient.setCACertBundle(x509_crt_bundle_start,
                              x509_crt_bundle_end - x509_crt_bundle_start);
    if (!client.setBufferSize(512)) {
        ESP_LOGW(TAG, "setBufferSize(512) fehlgeschlagen, PubSubClient nutzt vorhandenen Buffer");
    }
    client.setServer(cfg.server, cfg.port);
    client.setCallback(mqttCallback);
    // E-Paper Full-Refresh blockiert ~8s; längeres Keep-Alive verhindert Broker-Timeout bei zwei Refreshes hintereinander.
    client.setKeepAlive(60);
    client.setSocketTimeout(5);
    lastMqttAttemptAt = 0;
    mqttBackoffMs = 0;
    mqttCurrentBackoffMs = kMqttBackoffInitialMs;
}

bool mqttIsConnected() {
    return client.connected();
}

void mqttLoop() {
    MqttConfig cfg{};
    mqttCfgSnapshot(&cfg);
    if (cfg.server[0] == '\0') {
        return;
    }

    const unsigned long now = millis();
    const bool connected = client.connected();

    static bool wasConnected = false;
    if (wasConnected && !connected) {
        wlanSetStaPowerSaveMqttActive(false);
    }
    if (connected && !wasConnected) {
        lastMqttAttemptAt = 0;
        mqttBackoffMs = 0;
        mqttCurrentBackoffMs = kMqttBackoffInitialMs;
        wlanSetStaPowerSaveMqttActive(true);
    }
    wasConnected = connected;

    if (!connected) {
        /* lastMqttAttemptAt und mqttBackoffMs sind 0: erster Verbindungsversuch ohne Wartezeit. */
        if (now - lastMqttAttemptAt >= mqttBackoffMs) {
            lastMqttAttemptAt = now;
            mqttBackoffMs = mqttTryConnectSinglePass();
        }
    } else {
        client.loop();
    }
}

unsigned long mqttMillisUntilNextConnectAttempt() {
    if (client.connected()) {
        return 0;
    }
    const unsigned long now = millis();
    const unsigned long elapsed = now - lastMqttAttemptAt;
    if (elapsed >= mqttBackoffMs) {
        return 0;
    }
    return mqttBackoffMs - elapsed;
}
