# Konfiguration & NVS

Alle persistenten Einstellungen werden in der **NVS** (Non-Volatile Storage) des ESP32 gespeichert. Die Firmware nutzt vier Namespaces über `Preferences` (thread-safe via `g_nvsMutex` in `config/nvs_utils`).

## NVS-Namespaces

| Namespace | Modul | Beschreibung |
|-----------|-------|--------------|
| `wifi` | `wifi/wlan.cpp` | WLAN-Credentials |
| `mqtt` | `mqtt/config.cpp` | Broker-Konfiguration |
| `cfg` | `config/app_config.cpp`, `ota/ota.cpp` | App-Einstellungen, OTA-Check-Tag |
| `chaya` | `heart/counter.cpp` | Zähler und Baselines |

## Namespace `wifi`

| Key | Typ | Beschreibung |
|-----|-----|--------------|
| `cred_v1` | Bytes (packed) | Aktuelles Format: SSID + Passwort + Magic |
| `ssid` | String | Legacy-Format (Fallback) |
| `pass` | String | Legacy-Format (Fallback) |

Beim Speichern werden Legacy-Keys (`ssid`, `pass`) entfernt und nur `cred_v1` geschrieben.

**Schreiben:** `configSaveWiFiCredentials()` – aus Web POST `/wifi-connect` oder `/wifi-connect-commit`

## Namespace `mqtt`

| Key | Typ | Default | Beschreibung |
|-----|-----|---------|--------------|
| `server` | String | `""` | Broker-Hostname oder IP |
| `port` | Int | `8883` | MQTT-Port |
| `user` | String | `""` | MQTT-Username |
| `pass` | String | `""` | MQTT-Passwort |
| `topic_pub` | String | `chaya/to_b` | Sende-Topic |
| `topic_sub` | String | `chaya/to_a` | Empfangs-Topic |
| `partner_id` | String | `""` | Partner-Device-ID (6 Hex) |

**Schreiben:** `saveMQTTConfig()` – nach `mqttCfgApplyPendingToActive()` im Network-Task

### Sanitisierung beim Laden

- Ungültiger Server → geleert
- Ungültige Topics → Default wiederhergestellt
- Pub/Sub gleich → Defaults
- Ungültige Partner-ID → geleert
- Partner-ID = eigene ID → geleert
- Partner-ID gesetzt → Topics automatisch: `chaya/<own>`, `chaya/<partner>`

## Namespace `cfg`

| Key | Typ | Default | Beschreibung |
|-----|-----|---------|--------------|
| `rstPeriod` | UChar | `7` | Anzeige-Reset-Periode in UTC-Tagen (0=aus, 1–30) |
| `authEn` | UChar | `0` | Web-Auth aktiviert (0=aus, 1=an) |
| `upd_day` | UInt | `0` | Letzter OTA-Auto-Check (UTC-Kalendertag) |

**Schreiben:**
- `rstPeriod`, `authEn`: Web POST `/settings` (deferred via App-Task)
- `upd_day`: automatisch nach OTA-Check

### Reset-Periode (`rstPeriod`)

| Wert | Verhalten |
|------|-----------|
| `0` | Periodischer Reset deaktiviert |
| `1`–`30` | Alle N UTC-Tage: Baselines auf aktuelle Raw-Werte setzen |
| fehlend/ungültig | Default **7** Tage |

Der periodische Reset setzt nur die **Anzeige-Baselines** zurück (Display zeigt wieder 0). Die absoluten MQTT-Zähler (`heartCounter`, `heartSentCounter`) bleiben unverändert.

Zusätzlich: Wenn ein Anzeige-Delta ≥ **999** erreicht, wird die Baseline für diese Seite sofort nachgezogen.

## Namespace `chaya`

| Key | Typ | Default | Beschreibung |
|-----|-----|---------|--------------|
| `counter` | Int | `0` | Empfangener Zählerstand (absolut) |
| `sentCount` | Int | `0` | Gesendete Zählerstände (absolut) |
| `cntBase` | Int | `0` | RX-Anzeige-Baseline |
| `sntBase` | Int | `0` | TX-Anzeige-Baseline |
| `rstDay` | UInt | `UINT32_MAX` | Letzter periodischer Reset (UTC-Tag) |

**Speicher-Strategie:**
- Debounced: Saves nur alle **≥30 s** wenn sich der Wert geändert hat
- Flush vor Reboot/OTA: sofort speichern wenn dirty
- Während Factory Reset: NVS-Writes suspendiert

## RAM-Caches

Einige Werte werden zusätzlich im RAM gecacht (Atomics):

| Variable | Namespace-Key | Modul |
|----------|---------------|-------|
| `heartCounter` | `chaya/counter` | counter |
| `heartSentCounter` | `chaya/sentCount` | counter |
| `counterBaseline` | `chaya/cntBase` | counter |
| `sentCountBaseline` | `chaya/sntBase` | counter |
| `s_resetPeriodDaysCached` | `cfg/rstPeriod` | app_config |
| `s_webAuthEnabledCached` | `cfg/authEn` | app_config |

Aktive MQTT-Config (`mqttCfg`) lebt nur in `mqtt/config.cpp` – Zugriff über Snapshot/Pending-API.

## Factory Reset

Auslöser: Knopf **10 s** halten → `resetAllSettings()` in `wifi/wlan.cpp`

Ablauf:
1. `g_systemShutdownInProgress = true`
2. NVS-Saves für Zähler suspendieren
3. WiFi-Test abbrechen
4. Web-Session invalidieren
5. HTTP-Server stoppen, DNS/mDNS beenden
6. WiFi disconnect
7. **Alle vier Namespaces löschen:** `wifi`, `mqtt`, `cfg`, `chaya`
8. RAM-Zähler und Config-Caches zurücksetzen
9. Neustart → SoftAP `Chaya2MQTT`

## Konfigurationsänderung über Web-UI

| Einstellung | Route | Verarbeitung |
|-------------|-------|--------------|
| WiFi | POST `/wifi-connect` | Direkt NVS (STA) oder Test→Commit (AP) |
| MQTT | POST `/mqtt` | Pending → App-Task → Network-Task → NVS |
| Pairing | POST `/pairing` | Pending → App-Task → Network-Task → NVS |
| Reset-Periode | POST `/settings` | Pending → App-Task → NVS |
| Web-Auth | POST `/settings` | Pending → App-Task → NVS |

MQTT- und Settings-Änderungen werden **deferred** verarbeitet (nicht im HTTP-Handler), um Blockierung zu vermeiden.

## Weitere Dokumentation

- MQTT-Config-Details: [MQTT.md](MQTT.md)
- Web-Routen: [WEB_ADMIN.md](WEB_ADMIN.md)
- Sicherheit: [SECURITY.md](SECURITY.md)
- Zähler-Logik: [DISPLAY.md](DISPLAY.md)
