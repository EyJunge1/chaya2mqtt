#pragma once

namespace Mycila {
class ESPConnect;
}

class AsyncWebServer;

/** AsyncWebServer-Instanz (Port 80), gemeinsam mit MycilaESPConnect. */
AsyncWebServer& webAdminWebServer();

/** MQTT-Wartungsrouten (/mqtt, /heart-setup-exit) einmal registrieren. */
void webAdminRegisterMqttRoutes();

/** Wartungs-HTTP stoppen, sobald ein Broker konfiguriert ist. */
void webAdminMaybeStopMaintenanceIfBrokerConfigured();

/** Wartungs-HTTP starten, wenn kein Broker, WLAN verbunden und ESPConnect im Netzwerk. */
void webAdminMaybeStartMaintenance(Mycila::ESPConnect& espConnect);

/** True, solange der Wartungs-Webserver aktiv laeuft (kein Light-Sleep). */
bool webAdminIsMaintenanceHttpActive();

/** Wartungs-HTTP beenden (z. B. Factory Reset). */
void webAdminStopMaintenanceHttp();
