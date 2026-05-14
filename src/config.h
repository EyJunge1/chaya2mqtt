#pragma once

#include <cstdint>
#include <ctime>

struct MqttConfig {
    char server[128]    = "";
    uint16_t port       = 8883;
    char username[64]   = "";
    char password[64]   = "";
    char topicPub[128]  = "chaya/to_b";
    char topicSub[128]  = "chaya/to_a";
};

extern MqttConfig mqttCfg;

/** Incoming heart counter (last value received on topicSub, retained decimal payload); persisted in config.cpp. */
extern int heartCounter;
/** Outgoing heart counter (successful publishes; next publish sends this + 1); persisted in config.cpp. */
extern int heartSentCounter;
/** Baselines for periodic display reset (NVS); displayed delta = raw counter minus baseline (capped in display). */
extern int counterBaseline;
extern int sentCountBaseline;

/** UTC calendar day index since epoch (floor(epoch_seconds / 86400)); used for periodic counter reset. */
uint32_t calendarDaySinceEpochUtc(time_t utc);

void loadMQTTConfig();
void saveMQTTConfig();

/** Zählerstände aus NVS laden/speichern (Namespace chaya). */
void loadHeartCounter();
void loadHeartSentCounter();
/** @return true wenn NVS-Schreiben erfolgreich */
bool saveHeartCounter();
/** @return true wenn NVS-Schreiben erfolgreich */
bool saveHeartSentCounter();
/** NVS max. alle ~30 s bei geändertem Zähler (weniger Flash-Verschleiß). */
void maybeSaveHeartCounter();
void maybeSaveHeartSentCounter();
/** Sofort speichern, falls Zähler seit letztem Commit geändert (z. B. vor Neustart). */
void flushHeartCounterIfDirty();
void flushHeartSentCounterIfDirty();

/** Load counter baselines and last reset calendar day from NVS (namespace chaya). */
void loadCounterBaseline();
/** If NTP time is valid, roll display baselines daily or weekly (retained MQTT counters unchanged). */
void maybePeriodicallyResetCounters();

/** true = weekly reset, false = daily reset (NVS cfg/rstPeriod). */
bool configGetResetPeriodIsWeekly();
void configSetResetPeriodWeekly(bool weekly);

void setupWiFi();
void resetAllSettings();

/** Vor ESP.restart(): alle bekannten gpio_hold_en freigeben (Strapping-/Light-Sleep-Holds). */
void releaseGpioHoldBeforeRestart();

/** WLAN-Credentials speichern (Namespace wifi). */
bool configSaveWiFiCredentials(const char* ssid, const char* password);

/** Im AP-Setup-Modus (SoftAP Fallback). */
bool configIsApMode();

/** Captive-DNS bei AP sowie webAdminLoop; immer mit Haupt-loop aufrufen. */
void configLoop();

/** True im AP-Captive-Portal-Modus, damit kein Light-Sleep. */
bool configIsSetupPortalActive();
