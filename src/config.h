#pragma once

#include <cstdint>

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

/** WLAN-Credentials speichern (Namespace wifi). */
bool configSaveWiFiCredentials(const char* ssid, const char* password);

/** Im AP-Setup-Modus (SoftAP Fallback). */
bool configIsApMode();

/** Captive-DNS bei AP sowie webAdminLoop; immer mit Haupt-loop aufrufen. */
void configLoop();

/** True im AP-Captive-Portal-Modus, damit kein Light-Sleep. */
bool configIsSetupPortalActive();
