#include "mqtt.h"

#include "config.h"
#include "display.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstring>
#include <esp_random.h>

// ESP-IDF hat ein eingebautes Mozilla-CA-Bundle in libmbedtls.a (CONFIG_MBEDTLS_CERTIFICATE_BUNDLE).
extern const uint8_t x509_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");

#if defined(CORE_DEBUG_LEVEL) && CORE_DEBUG_LEVEL > 0
#define MQTT_DBG_PRINT(x) Serial.print(x)
#define MQTT_DBG_PRINTLN(x) Serial.println(x)
#else
#define MQTT_DBG_PRINT(x) ((void)0)
#define MQTT_DBG_PRINTLN(x) ((void)0)
#endif

static WiFiClientSecure espClient;
static PubSubClient client(espClient);

static constexpr unsigned long kMqttBackoffInitialMs = 5000;
static constexpr unsigned long kMqttBackoffMaxMs = 60000;

static unsigned long lastMqttAttemptAt = 0;
static unsigned long mqttBackoffMs = 0;
static unsigned long mqttCurrentBackoffMs = kMqttBackoffInitialMs;

/** Eine Verbindungsrunde; Rückgabe: Millisekunden bis zum nächsten Versuch. */
static unsigned long mqttTryConnectSinglePass() {
    MQTT_DBG_PRINT("Verbinde mit MQTT (TLS)...");
    char clientId[24];
    snprintf(clientId, sizeof(clientId), "ESP32Heart-%04lX",
             static_cast<unsigned long>(esp_random() & 0xffffU));

    MQTT_DBG_PRINT("Client ID: ");
    MQTT_DBG_PRINTLN(clientId);
    MQTT_DBG_PRINT("Server: ");
    MQTT_DBG_PRINT(mqtt_server);
    MQTT_DBG_PRINT(":");
    MQTT_DBG_PRINTLN(mqtt_port);

    if (strlen(mqtt_server) == 0) {
        MQTT_DBG_PRINTLN(
            "Kein MQTT-Server konfiguriert. Bitte Captive Portal erneut öffnen (Reset: Taste 5s halten).");
        return 60000;
    }

    if (WiFi.status() != WL_CONNECTED) { // NOLINT(readability-static-accessed-through-instance)
        MQTT_DBG_PRINTLN("WiFi nicht verbunden! Warte auf Reconnect in main loop...");
        return 5000;
    }

    // mqtt_topic_pub bis 127 Zeichen + "/lwt" (4) + NUL -> mind. 132 Byte; 140 fuer Rand.
    char willTopic[140];
    snprintf(willTopic, sizeof(willTopic), "%s/lwt", mqtt_topic_pub);

    if (client.connect(clientId, mqtt_username, mqtt_password, willTopic, 1, true, "offline", true)) {
        MQTT_DBG_PRINTLN("MQTT verbunden!");
        mqttCurrentBackoffMs = kMqttBackoffInitialMs;
        MQTT_DBG_PRINT("Subscribing zu Topic (QoS 1): ");
        MQTT_DBG_PRINTLN(mqtt_topic_sub);
        if (!client.subscribe(mqtt_topic_sub, 1)) {
            MQTT_DBG_PRINTLN("MQTT: Subscribe fehlgeschlagen.");
        }
        return 0;
    }

    MQTT_DBG_PRINT("MQTT fehlgeschlagen, rc=");
    MQTT_DBG_PRINT(client.state());
    MQTT_DBG_PRINTLN(" (0=Connection timeout, -1=Connection lost, -2=Connect failed, ...)");
    const unsigned long waitMs = mqttCurrentBackoffMs;
    mqttCurrentBackoffMs = std::min(mqttCurrentBackoffMs * 2UL, kMqttBackoffMaxMs);
    MQTT_DBG_PRINT("Nächster Versuch in ");
    MQTT_DBG_PRINT(waitMs / 1000UL);
    MQTT_DBG_PRINTLN(" s (exponentieller Backoff)...");
    return waitMs;
}

// NOLINTNEXTLINE(readability-non-const-parameter) - PubSubClient callback signature is fixed
static void mqttCallback(char* topic, byte* payload, unsigned int length) {
    (void)topic;
    static constexpr char kHeartPayload[] = "heart";
    static constexpr unsigned int kHeartLen = sizeof(kHeartPayload) - 1U;
    if (length != kHeartLen || memcmp(payload, kHeartPayload, kHeartLen) != 0) {
        MQTT_DBG_PRINTLN("MQTT: unerwarteter Payload ignoriert.");
        return;
    }

    MQTT_DBG_PRINT("Nachricht empfangen: ");
#if defined(CORE_DEBUG_LEVEL) && CORE_DEBUG_LEVEL > 0
    Serial.write(reinterpret_cast<const char*>(payload), length);
    Serial.println();
#endif

    if (heartCounter < INT_MAX) {
        heartCounter++;
    }
    requestHeartRedraw();
}

bool mqttPublishHeart() {
    if (!client.connected()) {
        MQTT_DBG_PRINTLN("MQTT nicht verbunden!");
        return false;
    }
    // Ein Publish-Versuch; Retries laufen nicht-blockierend in button.cpp (LED-State-Machine).
    static constexpr char kPayload[] = "heart";
    if (client.publish(mqtt_topic_pub, kPayload)) {
        return true;
    }
    client.loop();
    return false;
}

void mqttSetup() {
    espClient.setCACertBundle(x509_crt_bundle_start);
    client.setBufferSize(256);
    client.setServer(mqtt_server, mqtt_port);
    client.setCallback(mqttCallback);
    // E-Paper Full-Refresh blockiert ~8s; längeres Keep-Alive verhindert Broker-Timeout bei zwei Refreshes hintereinander.
    client.setKeepAlive(60);
    client.setSocketTimeout(5);
    lastMqttAttemptAt = 0;
    mqttBackoffMs = 0;
    mqttCurrentBackoffMs = kMqttBackoffInitialMs;
}

void mqttLoop() {
    const unsigned long now = millis();
    const bool connected = client.connected();

    static bool wasConnected = false;
    if (connected && !wasConnected) {
        lastMqttAttemptAt = 0;
        mqttBackoffMs = 0;
        mqttCurrentBackoffMs = kMqttBackoffInitialMs;
    }
    wasConnected = connected;

    if (!connected) {
        if (now - lastMqttAttemptAt >= mqttBackoffMs) {
            lastMqttAttemptAt = now;
            mqttBackoffMs = mqttTryConnectSinglePass();
        }
    } else {
        client.loop();
    }
}
