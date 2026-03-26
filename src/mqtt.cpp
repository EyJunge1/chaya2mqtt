#include "mqtt.h"

#include "config.h"
#include "display.h"

#include <Arduino.h>
#include <WiFi.h>
#include <cstring>

WiFiClientSecure espClient;
PubSubClient client(espClient);

namespace {

unsigned long nextMqttTryAt = 0;

/** Eine Verbindungsrunde; Rückgabe: Millisekunden bis zum nächsten Versuch. */
unsigned long mqttTryConnectSinglePass() {
    Serial.print("Verbinde mit MQTT (TLS)...");
    String clientId = "ESP32Heart-";
    clientId += String(random(0xffff), HEX);

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
        Serial.println("WiFi nicht verbunden! Versuche Wiederherstellung...");
        WiFi.reconnect();
        return 5000;
    }

    IPAddress serverIP;
    // NOLINTNEXTLINE(readability-static-accessed-through-instance,readability-implicit-bool-conversion)
    if (WiFi.hostByName(mqtt_server, serverIP) != 0) {
        Serial.print("DNS erfolgreich aufgelöst: ");
        Serial.println(serverIP);
    } else {
        Serial.println("DNS Auflösung fehlgeschlagen!");
        WiFi.reconnect();
        return 10000;
    }

    if (client.connect(clientId.c_str(), mqtt_username, mqtt_password)) {
        Serial.println("MQTT verbunden!");
        Serial.print("Subscribing zu Topic: ");
        Serial.println(mqtt_topic_sub);
        client.subscribe(mqtt_topic_sub);
        return 0;
    }

    Serial.print("MQTT fehlgeschlagen, rc=");
    Serial.print(client.state());
    Serial.println(" (0=Connection timeout, -1=Connection lost, -2=Connect failed, ...)");
    Serial.println("Versuche es in 5 Sekunden erneut...");
    return 5000;
}

} // namespace

// NOLINTNEXTLINE(readability-non-const-parameter) - PubSubClient callback signature is fixed
static void mqttCallback(char* topic, byte* payload, unsigned int length) {
    (void)topic;
    String message;
    message.reserve(length);
    for (unsigned int i = 0; i < length; i++) {
        message += static_cast<char>(payload[i]);
    }

    Serial.print("Nachricht empfangen: ");
    Serial.println(message);

    counter++;
    saveHeartCounter();
    requestHeartRedraw();
}

void mqttSetup() {
    espClient.setInsecure();
    client.setServer(mqtt_server, mqtt_port);
    client.setCallback(mqttCallback);
    nextMqttTryAt = 0;
}

void mqttLoop() {
    const unsigned long now = millis();

    if (!client.connected()) {
        if (nextMqttTryAt == 0 || now >= nextMqttTryAt) {
            const unsigned long backoffMs = mqttTryConnectSinglePass();
            nextMqttTryAt = now + backoffMs;
        }
    } else {
        nextMqttTryAt = 0;
    }

    client.loop();
}
