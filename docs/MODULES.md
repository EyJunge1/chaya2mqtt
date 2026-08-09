# Code-Referenz (Module)

Übersicht aller Quellmodule unter `src/` mit korrekten Pfaden, Verantwortlichkeiten und wichtigen APIs.

---

## `main.cpp`

**Zweck:** Bootstrap und Task-Start. Kein klassischer Arduino-`loop()`-Stil.

**Setup-Reihenfolge:**
1. `asyncInfraInit()` – Queues + Mutexe
2. CPU 240 MHz, BT aus, DFS (kein Light-Sleep)
3. `displayInit()` + `displayStartTask()`
5. `buttonInit()`
6. NVS laden: MQTT, Zähler, Reset-Periode, Web-Auth
7. `setupWiFi()` – STA oder AP
8. `mqttSetup()`
9. `buttonStartupBlink()` (vor Button-Task!)
10. `buttonStartTask()`, `networkTaskStart()`, `otaTaskStart()`, `appTaskStart()`
11. Deferred Draw: Herz oder Splash

**`loop()`:** `vTaskDelete(nullptr)` – beendet sich sofort.

---

## `async/` – Infrastruktur

### `async/event_types.h`

```cpp
enum class NetCmd : uint8_t {
    MqttSettingsChanged, MqttKillClient, WifiReconnect, ChayaSendRequested,
    FactoryResetRequested,
};

struct DisplayMsg {
    enum class Cmd : uint8_t { DrawHeart, DrawSplash, DrawAuthCode, DrawAuthPrompt };
    Cmd cmd;
    uint32_t payload;
};
```

### `async/task_handles.h` / `task_handles.cpp`

Globale Queues und Mutexe. `asyncInfraInit()` erstellt:
- `g_netCmdQueue` (32 × `NetCmd`)
- `g_displayCmdQueue` (32 × `DisplayMsg`)
- 6 Mutexe (siehe [ARCHITECTURE.md](ARCHITECTURE.md))

### `async/app_task.cpp`

App-Task (4096 Stack, Prio 4, Core 1), Loop alle 500 ms:
- `webAdminLoop()` – deferred Web-Arbeit, SSE
- `maybePeriodicallyResetCounters()` – nur im STA-Modus
- `maybeResetDisplayBaselinesWhenCapped()` – nur im STA-Modus
- `maybeSaveHeartCounter()` / `maybeSaveHeartSentCounter()`

---

## `heart/counter` – Zählerlogik

**Dateien:** `heart/counter.h`, `heart/counter_internal.h`, `heart/counter.cpp`, `heart/counter_nvs.cpp`, `heart/counter_sync.cpp`

| Datei | Verantwortung |
|-------|---------------|
| `counter.cpp` | Atomics, Display-Deltas, Factory-RAM-Reset |
| `counter_nvs.cpp` | NVS-Laden/Speichern, debounced Saves (≥30 s) |
| `counter_sync.cpp` | Periodischer Baseline-Roll, Cap-Reset bei ≥999 |

### Globale Variablen

| Symbol | Typ | Beschreibung |
|--------|-----|--------------|
| `heartCounter` | `std::atomic<int>` | Empfangener Stand (MQTT Subscribe) |
| `heartSentCounter` | `std::atomic<int>` | Erfolgreich gesendete Werte |
| `counterBaseline` | `std::atomic<int>` | Anzeige-Baseline RX |
| `sentCountBaseline` | `std::atomic<int>` | Anzeige-Baseline TX |

### Wichtige Funktionen

| Funktion | Beschreibung |
|----------|--------------|
| `heartDisplayRxDelta()` / `heartDisplayTxDelta()` | Delta = raw − baseline, gecappt |
| `heartCounterStoreFromRemote(int)` | Empfangenen Wert setzen (thread-safe) |
| `heartSentCounterApplyAfterSuccessfulPublish()` | TX-Zähler inkrementieren |
| `heartCounterFillDrawSnapshot(...)` | Atomarer Snapshot für Display |
| `loadHeartCounter()` / `saveHeartCounter()` | NVS `chaya` lesen/schreiben |
| `maybeSaveHeartCounter()` | Debounced Save (≥30 s) |
| `flushHeartCounterIfDirty()` | Sofort speichern wenn geändert |
| `maybePeriodicallyResetCounters()` | Periodischer Baseline-Roll (UTC-Tage) |
| `maybeResetDisplayBaselinesWhenCapped()` | Baseline-Roll bei Anzeige ≥999 |
| `counterResetRamAfterFactoryClear()` | RAM nach Factory Reset |

NVS-Debouncing: Saves nur alle **≥30 s** (`kHeartCounterSaveMinIntervalMs`).

---

## `mqtt/config` – Broker-Konfiguration

**Dateien:** `mqtt/config.h`, `mqtt/config.cpp`

Aktive `MqttConfig` ist statisch in `config.cpp`. Zugriff nur über API-Funktionen (Mutex-geschützt).

| Funktion | Beschreibung |
|----------|--------------|
| `loadMQTTConfig()` / `saveMQTTConfig()` | NVS `mqtt` lesen/schreiben |
| `mqttCfgSnapshot(MqttConfig*)` | Thread-safe Kopie |
| `mqttCfgStorePending(...)` | Web-Formular → Pending |
| `mqttCfgApplyPendingToActive()` | Pending → Active |
| `buildDeviceId(char*, size_t)` | 6-Hex-ID aus MAC |
| `mqttCfgApplyPairingTopics(MqttConfig*)` | Auto-Topics aus Partner-ID |

Sanitisierung beim NVS-Laden: ungültige Server/Topics/Partner-IDs werden bereinigt.

---

## `mqtt/mqtt` – MQTT-Client

**Dateien:** `mqtt/mqtt.h`, `mqtt/mqtt_internal.h`, `mqtt/mqtt_config.h`, `mqtt/mqtt_timing.h`, `mqtt/mqtt_client.cpp`, `mqtt/mqtt_events.cpp`, `mqtt/mqtt_publish.cpp`, `mqtt/mqtt_reconnect.cpp`

| Datei | Verantwortung |
|-------|---------------|
| `mqtt_client.cpp` | Client-Allokation, TLS, Mutex |
| `mqtt_events.cpp` | Event-Handler, Subscribe, Payload-Parsing |
| `mqtt_publish.cpp` | Chaya-Publish, Settings-Apply-Block |
| `mqtt_reconnect.cpp` | `mqttLoop()`, Prechecks, Backoff |

ESP-IDF `esp_mqtt_client` über `mqtts://` mit TLS-Bundle (`tls/`).

| Funktion | Beschreibung |
|----------|--------------|
| `mqttSetup()` | Client reset, Backoff zurücksetzen |
| `mqttLoop()` | Reconnect-Logik, Prechecks, Client-Init |
| `mqttDisconnect()` | Client stoppen und zerstören |
| `mqttPublishChaya()` | Publish `heartSentCounter + 1` (retained, QoS 0) |
| `mqttPublishChayaAndApplySentCounters()` | Publish + TX-Zähler inkrementieren |
| `mqttIsConnected()` | Verbindungsstatus |
| `mqttPublishBlocked()` | True während Settings-Apply |
| `mqttBeginSettingsApply()` / `mqttEndSettingsApply()` | Publish-Sperre |

Event-Handler (`MQTT_EVENT_DATA`): Payload parsen → `heartCounterStoreFromRemote()` → Display-Redraw.

---

## `wifi/wlan` – WLAN & Captive Portal

**Dateien:** `wifi/wlan.h`, `wifi/wlan_config.h`, `wifi/wlan_internal.h`, `wifi/wlan.cpp`, `wifi/wlan_boot.cpp`, `wifi/wlan_events.cpp`, `wifi/wlan_nvs.cpp`, `wifi/wlan_scan.cpp`

| Datei | Verantwortung |
|-------|---------------|
| `wlan.cpp` | Globaler State, `wlanLoop()`, Factory Reset, API-Lock |
| `wlan_boot.cpp` | `setupWiFi()`, STA/AP-Fallback, mDNS/NTP |
| `wlan_events.cpp` | STA-Events, Reconnect-Backoff |
| `wlan_nvs.cpp` | NVS-Credentials (packed `cred_v1`) |
| `wlan_scan.cpp` | Scan-Cache, Refresh |

| Funktion | Beschreibung |
|----------|--------------|
| `setupWiFi()` | Routes registrieren, STA oder AP `Chaya2MQTT`, Server starten |
| `wlanLoop()` | Captive DNS, mDNS-Restart, WiFi-Scan-Service |
| `configSaveWiFiCredentials(...)` | NVS `wifi` schreiben (packed `cred_v1`) |
| `configIsApMode()` | SoftAP-Einrichtungsmodus? |
| `resetAllSettings()` | Factory Reset: NVS löschen, Neustart |
| `wlanStaConnectedOk()` | STA verbunden + IP? |
| `wlanStaStableForMqtt()` | STA ≥3 s nach GOT_IP stabil? |
| `wlanNtpSynced()` | NTP-Zeit plausibel? |
| `wlanSetStaPowerSaveMqttActive(bool)` | Modem-Sleep bei aktiver MQTT-Session |
| `wlanWifiScanCopySnapshot(...)` | Scan-Ergebnisse (max. 40 APs) |
| `wlanHandleStaReconnectNetCmd()` | Reconnect mit Backoff |

### `wifi/test` – Verbindungstest

**Dateien:** `wifi/test.h`, `wifi/test.cpp`

AP-Modus: testet STA-Verbindung vor dem Speichern der Credentials.

---

## `network/network_task` – Netzwerk-Orchestrierung

**Dateien:** `network/network_task.h`, `network/network_task.cpp`

Network-Task (7168 Stack, Prio 5, Core 1):
- `NetCmd`-Queue verarbeiten (500 ms Timeout)
- `wlanLoop()` jeden Zyklus
- `mqttLoop()` nur im STA-Modus

---

## `display/` – E-Ink

**Dateien:** `display/display.h`, `display/display_config.h`, `display/display.cpp`, `display/draw.cpp`, `display/internal.h`

### Display-Task

Nur dieser Task darf SPI/EPD ansprechen. Befehle über `g_displayCmdQueue`.

| Funktion | Beschreibung |
|----------|--------------|
| `displayInit()` | SPI + EPD initialisieren |
| `displayStartTask()` | FreeRTOS-Task (4096 Stack, Prio 3) |
| `requestHeartRedraw()` | Herz neu zeichnen (blockierend, 100 ms Queue-Timeout) |
| `requestHeartRedrawNonBlocking()` | Herz neu zeichnen (0 ms Timeout, für MQTT-Callback) |
| `requestDeferredDrawAuthCode(code)` | Auth-Code auf E-Ink |
| `requestDeferredDrawAuthPrompt()` | „Web Auth?" auf E-Ink |
| `requestDeferredDrawSplashScreen()` | Splash bei fehlendem Broker |
| `requestDeferredDrawHeartScreen()` | Herz nach Setup |

Details zur Geometrie: [DISPLAY.md](DISPLAY.md)

### GxEPD2 (PlatformIO)

**Dependency:** `ZinggJM/GxEPD2` in `platformio.ini`

**Typ in Firmware:** `ChayaEpdPanel` = `GxEPD2_3C<GxEPD2_154_Z90c, GxEPD2_154_Z90c::HEIGHT>` (`display/internal.h`)

- Panel: SSD1682 / GDEH0154Z90, 200×200, 3-Farben (BWR)
- Full-Window-Refresh only (~8–14 s)
- Kein Partial Refresh

---

## `hw/button` – Taster & LED

**Dateien:** `hw/button.h`, `hw/button_config.h`, `hw/button_internal.h`, `hw/button_input.cpp`, `hw/button_led.cpp`, `hw/pins.h`

| Datei | Verantwortung |
|-------|---------------|
| `button_input.cpp` | GPIO/ISR, Debounce, Factory Reset → `NetCmd` |
| `button_led.cpp` | LED-Sequenz, Auth-Blink, MQTT-Publish nach Blink |

| Konstante | Wert | Bedeutung |
|-----------|------|-----------|
| `kButtonGpio` | GPIO 2 | Taster (`INPUT_PULLDOWN`) |
| `kButtonLedPin` | GPIO 4 | LED |
| `kFactoryResetHoldMs` | 10000 | Factory Reset (10 s Halten) |
| `kShortPressMinMs` | 50 | Mindestdauer Kurzdruck |

Button-Task (4096 Stack, Prio 8, Core 1):
- Debounce (~20 ms)
- Kurzdruck → MQTT-Sende-LED-Sequenz (2× Blink → Publish → 2× Blink)
- 10 s Halten → `NetCmd::FactoryResetRequested` in Network-Task (`resetAllSettings()` WDT-sicher dort)
- Web-Auth: langsamer Blink, Kurzdruck bestätigt Prompt

| Funktion | Beschreibung |
|----------|--------------|
| `buttonInit()` | GPIO initialisieren |
| `buttonStartTask()` | FreeRTOS-Task starten |
| `buttonStartupBlink()` | 3× 200 ms Blink (blockierend, nur Setup) |
| `buttonIsLedTxSequenceActive()` | MQTT-Sende-Sequenz läuft? |
| `buttonSetAuthBlinkShortPressHandler(fn)` | Callback für Auth-Kurzdruck |
| `buttonSetAuthBlinkActive(bool)` | Auth-LED-Blink steuern |

---

## `web/` – Admin-Oberfläche

| Datei | Zweck |
|-------|-------|
| `admin.h` / `admin.cpp` | Server-Singleton, Route-Registrierung, `webAdminLoop()` |
| `admin_globals.h` / `admin_globals.cpp` | Shared Atomics/Flags |
| `admin_json.h` | JSON-Helper für kleine Antworten |
| `deferred_reboot.h` / `deferred_reboot.cpp` | Reboot nach WiFi-Save |
| `web_utils.h` / `web_utils.cpp` | Redirects, Security-Headers |
| `web_middleware.h` / `web_middleware.cpp` | CSRF-Middleware für AP-Routen |
| `web_events.h` / `web_events.cpp` | SSE `/events` |
| `routes/admin_routes_api.cpp` | JSON-API `/api/*` für die React-SPA |
| `routes/admin_routes_spa.cpp` | SPA `index.html` + `/assets/app.{js,css}` (gzip/PROGMEM) |
| `routes/admin_routes_wifi.cpp` / `mqtt` / `app` | Legacy-Stubs (Logik in API/SPA) |
| `auth/auth.h` / `auth/auth_session.cpp` / `auth/auth_routes.cpp` / `auth/auth_challenge.cpp` | Session, CSRF, Challenge-Flow |
| `assets/spa_*.h` | Generierte gzip-SPA-Assets (PROGMEM) |

Details: [WEB_ADMIN.md](WEB_ADMIN.md)

---

## `ota/` – Firmware-Updates

| Datei | Zweck |
|-------|-------|
| `ota.h` / `ota.cpp` | Auto-Check-Logik, Download-Queue |
| `ota_task.cpp` | OTA-Task (8192 Stack, Prio 4) |
| `github.h` / `github.cpp` | GitHub Releases API, semver-Vergleich |
| `flash.h` / `flash.cpp` | TLS-Download, SHA256, Flash-Install |

| Funktion | Beschreibung |
|----------|--------------|
| `otaLoop()` | Täglicher Auto-Check + ausstehender Download |
| `otaQueueGithubCheck()` | Manuellen Check anstoßen |

Details: [OTA.md](OTA.md)

---

## `config/` – Anwendungskonfiguration

### `config/app_config`

| Funktion | Beschreibung |
|----------|--------------|
| `configGetResetPeriodDays()` | 0=aus, 1–30 Tage (Default 7) |
| `configSetResetPeriodDays(uint8_t)` | NVS `cfg/rstPeriod` |
| `configGetWebAuthEnabled()` | Web-Auth an? |
| `configSetWebAuthEnabled(bool)` | NVS `cfg/authEn` |

### `config/nvs_utils`

Thread-safe `Preferences`-Wrapper mit `g_nvsMutex`:
- `readInt`, `writeInt`, `readUInt`, `writeUInt`, `readUChar`, `writeUChar`
- `clearNamespace(const char*)`

---

## `diag/` – Diagnose

| Datei | Zweck |
|-------|-------|
| `task_watchdog.h` | `chayaTaskWatchdogSubscribe()` / `Unsubscribe()` / `Reset()` |
| `stack_monitor.h` | Periodisches Stack-High-Water-Logging |

---

## `tls/` – TLS CA-Bundle

| Datei | Zweck |
|-------|-------|
| `tls/tls_bundle.h` | Eingebettete X509-CA-Zertifikate (PROGMEM) |
| `tls/tls_bundle_setup.h` / `tls_bundle_setup.cpp` | Einmalige CA-Bundle-Initialisierung (Mutex, von MQTT + OTA genutzt) |

---

## Hilfsmodule

| Datei | Zweck |
|-------|-------|
| `constants.h` | Geräteweite Identity, NTP, Syntax-Validierung (cross-module) |
| `mqtt/mqtt_config.h` | MQTT-Protokoll-Defaults (Topics, Port, Keepalive, Outbox) |
| `mqtt/mqtt_timing.h` | MQTT-Backoff, Lock-Timeouts |
| `wifi/wlan_config.h` | Wi-Fi-Limits, Connection-Tuning, Scan/Reconnect-Intervalle |
| `display/display_config.h` | Display-Limits (`kDisplayCounterMax`) |
| `hw/button_config.h` | Button/LED-Timing |
| `web/auth/auth_config.h` | Auth-Timing (Challenge, Session, Lockout) |
| `config/version.h` | `APP_VERSION` (CI setzt aus Git-Tag) |
| `util/log_tag.h` | `DEFINE_LOG_TAG` Makro |
| `util/ip_format.h` | IP-Adress-Formatierung |
| `util/time_helpers.h` | Wrap-sichere Zeithelfer (`elapsedMs`, `deadlineReached`, `remainingMs`) |
| `config/nvs_keys.h` | Zentrale NVS-Namespace- und Key-Konstanten |
| `async/task_config.h` | FreeRTOS Task-Stack-Größen und Queue-Tiefen |

---

## Querverweise

- Architektur: [ARCHITECTURE.md](ARCHITECTURE.md)
- MQTT: [MQTT.md](MQTT.md)
- Web-Admin: [WEB_ADMIN.md](WEB_ADMIN.md)
- Hardware: [HARDWARE.md](HARDWARE.md)
- Konfiguration: [CONFIGURATION.md](CONFIGURATION.md)
