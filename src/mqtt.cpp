#include "mqtt.h"

#include "config.h"
#include "display.h"

#include <Arduino.h>
#include <WiFi.h>
#include <cstring>

WiFiClientSecure espClient;
PubSubClient client(espClient);

// NOLINTNEXTLINE(readability-non-const-parameter) - PubSubClient callback signature is fixed
static void mqttCallback(char* topic, byte* payload, unsigned int length) {
    (void)topic;
    String message = "";
    for (unsigned int i = 0; i < length; i++) {
        message += (char)payload[i];
    }

    Serial.print("Nachricht empfangen: ");
    Serial.println(message);

    counter++;
    drawHeartWithNumber();
}

void mqttSetup() {
    espClient.setInsecure();
    client.setServer(mqtt_server, mqtt_port);
    client.setCallback(mqttCallback);
}

void mqttReconnect() {
    while (!client.connected()) {
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
            delay(10000);
            continue;
        }

        if (WiFi.status() != WL_CONNECTED) { // NOLINT(readability-static-accessed-through-instance)
            Serial.println("WiFi nicht verbunden! Versuche Wiederherstellung...");
            WiFi.reconnect();
            delay(5000);
            continue;
        }

        IPAddress serverIP;
        // NOLINTNEXTLINE(readability-static-accessed-through-instance,readability-implicit-bool-conversion)
        if (WiFi.hostByName(mqtt_server, serverIP) != 0) {
            Serial.print("DNS erfolgreich aufgelöst: ");
            Serial.println(serverIP);
        } else {
            Serial.println("DNS Auflösung fehlgeschlagen!");
            WiFi.reconnect();
            delay(10000);
            continue;
        }

        if (client.connect(clientId.c_str(), mqtt_username, mqtt_password)) {
            Serial.println("MQTT verbunden!");
            Serial.print("Subscribing zu Topic: ");
            Serial.println(mqtt_topic_sub);
            client.subscribe(mqtt_topic_sub);
        } else {
            Serial.print("MQTT fehlgeschlagen, rc=");
            Serial.print(client.state());
            Serial.println(" (0=Connection timeout, -1=Connection lost, -2=Connect failed, ...)");
            Serial.println("Versuche es in 5 Sekunden erneut...");
            delay(5000);
        }
    }
}

void mqttLoop() {
    if (!client.connected()) {
        mqttReconnect();
    }
    client.loop();
}
