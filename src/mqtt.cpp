#include "mqtt.h"

#include "config.h"
#include "display.h"

#include <Arduino.h>
#include <WiFi.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <esp_random.h>

WiFiClientSecure espClient;
PubSubClient client(espClient);

static constexpr unsigned long kMqttBackoffInitialMs = 5000;
static constexpr unsigned long kMqttBackoffMaxMs = 60000;

static unsigned long lastMqttAttemptAt = 0;
static unsigned long mqttBackoffMs = 0;
static unsigned long mqttCurrentBackoffMs = kMqttBackoffInitialMs;

/** Eine Verbindungsrunde; Rückgabe: Millisekunden bis zum nächsten Versuch. */
static unsigned long mqttTryConnectSinglePass() {
    Serial.print("Verbinde mit MQTT (TLS)...");
    char clientId[24];
    snprintf(clientId, sizeof(clientId), "ESP32Heart-%04lX", static_cast<unsigned long>(random(0xffff)));

    Serial.print("Client ID: ");
    Serial.println(clientId);
    Serial.print("Server: ");
    Serial.print(mqtt_server);
    Serial.print(":");
    Serial.println(mqtt_port);

    if (strlen(mqtt_server) == 0) {
        Serial.println(
            "Kein MQTT-Server konfiguriert. Bitte Captive Portal erneut öffnen (Reset: Taste 5s halten).");
        return 10000;
    }

    if (WiFi.status() != WL_CONNECTED) { // NOLINT(readability-static-accessed-through-instance)
        Serial.println("WiFi nicht verbunden! Warte auf Reconnect in main loop...");
        return 5000;
    }

    if (client.connect(clientId, mqtt_username, mqtt_password)) {
        Serial.println("MQTT verbunden!");
        mqttCurrentBackoffMs = kMqttBackoffInitialMs;
        Serial.print("Subscribing zu Topic (QoS 1): ");
        Serial.println(mqtt_topic_sub);
        client.subscribe(mqtt_topic_sub, 1);
        return 0;
    }

    Serial.print("MQTT fehlgeschlagen, rc=");
    Serial.print(client.state());
    Serial.println(" (0=Connection timeout, -1=Connection lost, -2=Connect failed, ...)");
    const unsigned long waitMs = mqttCurrentBackoffMs;
    mqttCurrentBackoffMs = std::min(mqttCurrentBackoffMs * 2UL, kMqttBackoffMaxMs);
    Serial.print("Nächster Versuch in ");
    Serial.print(waitMs / 1000UL);
    Serial.println(" s (exponentieller Backoff)...");
    return waitMs;
}

// NOLINTNEXTLINE(readability-non-const-parameter) - PubSubClient callback signature is fixed
static void mqttCallback(char* topic, byte* payload, unsigned int length) {
    (void)topic;
    static constexpr char kHeartPayload[] = "heart";
    static constexpr unsigned int kHeartLen = sizeof(kHeartPayload) - 1U;
    if (length != kHeartLen || memcmp(payload, kHeartPayload, kHeartLen) != 0) {
        Serial.println("MQTT: unerwarteter Payload ignoriert.");
        return;
    }

    Serial.print("Nachricht empfangen: ");
    Serial.write(reinterpret_cast<const char*>(payload), length);
    Serial.println();

    counter++;
    requestHeartRedraw();
}

bool mqttPublishHeart() {
    if (!client.connected()) {
        Serial.println("MQTT nicht verbunden!");
        return false;
    }
    return client.publish(mqtt_topic_pub, "heart");
}

void mqttSetup() {
    // Echte Zufallswerte für Arduino random() (Client-ID), nicht nur libc-rand().
    randomSeed(static_cast<unsigned long>(esp_random()));
    // Heimnetz: keine Zertifikatsprüfung (Man-in-the-Middle möglich). Für Produktion Root-CA einbinden.
    espClient.setInsecure();
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
