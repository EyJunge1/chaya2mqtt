#pragma once

#include <cstdint>

namespace Mycila {
class ESPConnect;
}

struct MqttConfig {
    char server[128]    = "";
    uint16_t port       = 8883;
    char username[64]   = "";
    char password[64]   = "";
    char topicPub[128]  = "heart/to_b";
    char topicSub[128]  = "heart/to_a";
};

extern MqttConfig mqttCfg;

/** Herz-Zähler (Anzeige + MQTT); Persistenz in config.cpp. */
extern int heartCounter;

void loadMQTTConfig();
void saveMQTTConfig();

/** Zählerstand aus NVS laden/speichern (Namespace heart). */
void loadHeartCounter();
/** @return true wenn NVS-Schreiben erfolgreich */
bool saveHeartCounter();
/** NVS max. alle ~30 s bei geändertem Zähler (weniger Flash-Verschleiß). */
void maybeSaveHeartCounter();
/** Sofort speichern, falls Zähler seit letztem Commit geändert (z. B. vor Neustart). */
void flushHeartCounterIfDirty();
void setupWiFi();
void resetAllSettings();

/** ESPConnect-Loop; immer zusammen mit Haupt-loop aufrufen. */
void configLoop();
/** True bei Captive Portal oder MQTT-Wartungs-HTTP (/mqtt), damit kein Light-Sleep. */
bool configIsSetupPortalActive();
/** Captive Portal beim naechsten Boot (Taste 5–12 s losgelassen → Neustart). */
void requestSetupPortalFromButton();

/** Referenz auf den Netzwerk-Manager (WiFi / Captive Portal). */
Mycila::ESPConnect& configEspConnect();
