# Code-Referenz (Module)

Uebersicht der Quellen unter `src/` (u.a. `hw/`, `wifi/`, `mqtt/`, `network/`, `async/`, `display/`, `heart/`, `ota/`, `web/` samt `web/assets/`).

---

## `main.cpp`

**Zweck:** Bootstrap, Task-Start; kein klassischer Arduino-**`loop()`**-Stil (siehe auch `network_task`, `app_task`).

**Setup (gekuerzt):** `asyncInfraInit`, 240 MHz, BT aus, DFS; Display-Init + Task; `buttonInit`; NVS MQTT/Heart/CFG; `setupWiFi`; `mqttSetup`; **`buttonStartupBlink` vor `buttonStartTask`**; `networkTaskStart`, `otaTaskStart`, `appTaskStart`; deferred Draw-Requests.

**Laufzeit:** Schleifen verteilt auf **network** (WLAN/MQTT), **app** (`webAdminLoop`, SSE, Heartkeeping), **OTA**, **display**, **button**. `loop()` ruft `vTaskDelete`.

---

## `src/mqtt/config.h` / `src/mqtt/config.cpp`

**Zweck:** MQTT-Broker-Konfiguration (NVS `mqtt`). Aktive **`MqttConfig`** ist in `config.cpp` statisch; Zugriff nur ueber **`mqttCfgSnapshot`**, **`mqttCfgStorePending`**, **`mqttCfgApplyPendingToActive`**, **`mqttCfgTopicPubLockedCopy`** usw.

### Funktionen (Auszug)

| Funktion | Beschreibung |
|----------|--------------|
| `loadMQTTConfig()` / `saveMQTTConfig()` | NVS Lesen/Schreiben |

---

## `counter.h` / `counter.cpp`

**Zweck:** Empfangs-/Sendezähler, NVS-Persistenz (Namespace `chaya`), Anzeige-Baselines, optional periodischer Baseline-Roll (UTC, NTP, Intervall 0–30 Tage, Default 7), NVS `cfg` fuer Reset-Periode; bei Anzeige >= 999 pro Seite Baseline-Roll.

### Globale Variablen

| Symbol | Beschreibung |
|--------|----------------|
| `heartCounter` | Zählerstand vom subscribed Topic (retained) |
| `heartSentCounter` | Erfolgreich gesendete Werte (nächster Publish = +1) |
| `counterBaseline`, `sentCountBaseline` | Basis fuer auf dem Display gezeigte Deltas |

Wichtige Funktionen: `loadHeartCounter`, `saveHeartCounter`, `maybeSaveHeartCounter`, `flushHeartCounterIfDirty`, gleiches fuer `HeartSent`, `loadCounterBaseline`, `maybePeriodicallyResetCounters`, `maybeResetDisplayBaselinesWhenCapped`, `configLoadResetPeriodFromNvs`, `configGetResetPeriodDays` / `configSetResetPeriodDays`, `counterResetRamAfterFactoryClear()`.

**Abhaengigkeit:** `maybePeriodicallyResetCounters()` und `maybeResetDisplayBaselinesWhenCapped()` pruefen `configIsApMode()` aus **wlan** (kein Roll im AP).

---

## `src/net/wlan.h` / `src/net/wlan.cpp`

**Zweck:** WiFi STA/AP, Captive DNS (`DNSServer`), mDNS, NTP nach STA-Verbindung, Reconnect-Backoff (`WiFi.onEvent`), gemeinsamer HTTP-Server mit **web_admin**; Factory Reset.

| Funktion | Beschreibung |
|----------|--------------|
| `setupWiFi()` | `webAdminRegisterRoutes()`, Credentials aus NVS `wifi`, STA oder AP `Chaya2MQTT`, `webAdminWebServer().begin()` |
| `wlanLoop()` | Captive DNS im AP, mDNS-Restart nach GOT_IP |
| `configSaveWiFiCredentials()` | NVS `wifi` schreiben |
| `configIsApMode()` | SoftAP-Einrichtungsmodus |
| `resetAllSettings()` | Server stoppen, NVS `wifi`/`mqtt`/`cfg`/`chaya` leeren, RAM-Zähler zurücksetzen, Neustart |
| `releaseGpioHoldBeforeRestart()` | GPIO-Hold vor Neustart |

**Hinweis:** Dateiname **wlan** vermeidet Konflikt mit Arduino `#include <WiFi.h>` auf case-insensitiven Dateisystemen.

---

## `src/web/admin.h` / `admin.cpp`

**Zweck:** `AsyncWebServer` (Singleton Port 80), Route-Registrierung, POST-Handler; **`webAdminLoop()`** wendet MQTT-Formular an, Reboot/Wi‑Fi-Neustart, ruft **`otaLoop()`**.

---

## `src/web/auth.h` / `auth.cpp`

**Zweck:** Zugriffsschutz für die Admin-UI: Session (`chaya_sid`), CSRF, `/auth` GET/POST, `/logout`; Redirects für unauthentifizierte Requests; `webAuthLoop()` (Ablauf 10 s „Warten auf Tastendruck“ nach sichtbarem Hinweis-Display — `webAuthResetConfirmDeadline()` startet dieses Fenster erst nach Ende des E‑Ink-`drawAuthPrompt()`, und dort ruft die Display-Pipeline ebenfalls `buttonSetAuthBlinkActive(true)` auf, damit der Auth‑LED‑Blink erst danach läuft; bis dahin bleibt die LED aus — und 5 min Code-Challenge). **`webAuthLoop`** lässt das 10 s‑Fenster erst ablaufen wenn `buttonIsAuthBlinkActive()` (Prompt wirklich sichtbar/Blink aktiv), sonst Race mit zeitlich versetztem Draw; nach Tastenbestätigung **`buttonSetAuthBlinkActive(false)`** bis **`drawAuthCode`** fertig, dann wieder Blink für die Code‑Phase). Mehrfachfehlversuche → Sperre (siehe Konstanten in `auth.cpp`).

---

## `src/net/ota.h` / `src/net/ota.cpp`

**Zweck:** GitHub `releases/latest` (JSON `tag_name` via ArduinoJson mit Legacy-Fallback), täglicher Auto-Check (NVS `cfg`/`upd_day`), manueller Check-Button, Firmware-Install über `HTTPUpdate` (TLS + CA-Bundle).

| Funktion | Beschreibung |
|----------|--------------|
| `otaLoop()` | Auto-Update-Logik + ausstehender Download |
| `otaQueueGithubCheck()` | Manueller Versions-Vergleich |

---

## `src/web/pages.h` / `pages.cpp`

**Zweck:** HTML-Streaming (Dashboard, Wi‑Fi-Scan-JSON, MQTT-Formular, Settings, Update-Seite); gemeinsames CSS via `src/web/assets/styles.h`, Wi‑Fi-Scan-JS via `src/web/assets/wifi_scan_js.h`.

**Hinweis:** Die Header `styles.h` und `wifi_scan_js.h` sind eingebettete Assets; bei einem zukünftigen Build-Skript die Ausgabe nach `src/web/assets/` schreiben.

---

## `src/hw/display.h` / `src/hw/display.cpp`

**Zweck:** E-Paper ansteuern und Herz mit Zaehlerstand zeichnen.

### Globale Symbole

| Symbol | Beschreibung |
|--------|--------------|
| Display-Instanz | nur in `src/hw/display.cpp` (`static`), nicht exportiert -- CS=15, DC=27, RST=26, BUSY=25 |

Der Zaehlerstand und Baselines kommen aus **`counter.h`**.

### Oeffentliche Funktionen

| Funktion | Beschreibung |
|----------|--------------|
| `displayInit()` | `SPI.begin(13, 12, 14, 15)`; `display.init(...)` mit **115200** nur wenn `CORE_DEBUG_LEVEL > 0`, sonst **0** (kein `Serial.begin` durch GxEPD2 im Release) |
| `drawHeartWithNumber()` | Vollbild-Refresh: weisser Hintergrund, rotes Herz aus zwei Kreisen, `fillTriangle` fuer die Spitze, gefuellter Bereich, schwarze Zahl unten mittig (`setTextSize` **2--4** je nach Stellenzahl); danach `display.hibernate()` (Controller Deep Sleep, Bild bleibt bistabil) |
| `drawAuthPrompt()` | Mittig „Web Auth?“ (Hinweis vor Tastenbestätigung) |
| `drawAuthCode(unsigned)` | Mittig nur 6‑stelliger Login-Code (ohne zusätzliche Zeilen) |
| `requestDeferredDrawAuthPrompt()`, `requestDeferredDrawAuthCode()` | E-Ink-Draw aus Web-Task in die Main Task einreihen (`displayProcessDeferredDrawsOnMainTask`) |
| `requestHeartRedraw()` | Setzt internes Flag (nach MQTT-Empfang) |
| `consumeHeartRedraw()` | Liefert `true` einmalig wenn Neuzeichnen angefordert; loescht das Flag |

### Implementierungsdetails

- Zeichnung in `firstPage()` / `nextPage()`-Schleife (partial window = full window).
- Herz-Geometrie: `static constexpr` Konstanten im Quellcode (Feintuning dort).
- `getTextBounds()` fuer die Zahl **nach** dynamischem `setTextSize` (korrekte Zentrierung); Randpruefung fuer das Dreieck mit `display.width()` / `display.height()`; Stellenanzahl aus `snprintf`-Rueckgabewert (kein `strlen`).
- Zeichenfortschritt-Serial nur bei `CORE_DEBUG_LEVEL > 0`.
- `g_heartRedrawPending` als **`std::atomic<bool>`** (Callback setzt Flag, `loop()` liest nach Light-Sleep).

---

## `src/mqtt/mqtt.h` / `src/mqtt/mqtt.cpp`

**Zweck:** MQTT ueber TLS mit **ESP-IDF `esp_mqtt_client`** (kein PubSubClient); Verbindung halten; bei Nachricht Counter setzen und Display aktualisieren.

### Kapselung

Der MQTT-Client-Handle und Event-Handler liegen nur in `mqtt.cpp`; `mqtt.h` exportiert keine TLS-Typen.

### Oeffentliche Funktionen

| Funktion | Beschreibung |
|----------|--------------|
| `mqttSetup()` | Backoff/`s_connected`/Pending-Flags zuruecksetzen; vorhandenen Client stoppen und zerstoeren (`esp_mqtt_client_stop`/`destroy`). Neuer Verbindungsaufbau erfolgt erst in `mqttLoop()` sobald WiFi und NTP stehen |
| `mqttLoop()` | Wenn keine Broker-URL: Client beenden. Wenn nicht verbunden und kein Pending-Handshake: Nach Backoff Precheck (leerer Server **60 s**, kein STA **20 s**, NTP/Stabilitaet **2 s**) `esp_mqtt_client_init`/`start`; bei TCP/TLS-Verlust: exponentieller Backoff **30 s** bis **60 s** (bei schwachem STA bis **90 s`). Kein `client.loop()` — MQTT laeuft im eigenen ESP-IDF-Task. Bei unbeabsichtigtem Disconnect zerstoert die Schleife den Client (**nicht im MQTT-Event-Handler**) und versucht später neu |
| `mqttPublishChaya()` | Publiziert `heartSentCounter + 1` als Dezimalstring (**retained**, **QoS 0** wie zuvor PubSubClient) auf `mqtt_topic_pub`, wenn verbunden; **2** Versuche nicht-blockierend in `button.cpp` (Phase `PublishRetryWait`) |

### Empfang (`MQTT_EVENT_DATA`)

- Payload als Dezimalstring (max. 10 Zeichen, `strtol`, `errno == ERANGE`); Ungueltiges wird ignoriert.
- Vergleich des Ziel-Topics per Laenge und `memcmp` gegen das bei CONNECT zwischengespeicherte Subscribe-Topic (aus aktueller Config / Cache in `mqtt.cpp`).
- `heartCounter` wird **gesetzt** (kein `++`); `requestHeartRedraw()` nur bei geaenderter Zahl.

### Implementierungsdetails

- TLS: Projekt-X509-Bundle ueber `esp_crt_bundle_set` + Broker-`verification.crt_bundle_attach = esp_crt_bundle_attach`; `WiFiClientSecure` bleibt separat fuer OTA.
- MQTT-Subscribe **QoS 1**; Zaehler-Publish wie frueher **QoS 0**, retained.
- Last Will: `{topic_pub}/lwt`, `offline`, **QoS 1**, retain; nach CONNECT retained `online` auf dasselbe Topic.
- `network.disable_auto_reconnect == true`; Reconnect-/Backoff-Taktung nur in `mqttLoop()`.
- Puffergroesse MQTT **512** Bytes; MQTT-Task-Stack groesser als Arduino-Minimum (wie frueherer TLS-Verbindungs-Task).
- Client-ID: `Chaya2MQTT-` + `esp_random()`-Hex.
- Debug: `ESP_LOG` abhaengig von `CORE_DEBUG_LEVEL` (wie ueblich).

---

## `src/hw/button.h` / `src/hw/button.cpp`

**Zweck:** Taster mit LED; Kurzdruck sendet MQTT; Langdruck setzt Geraet zurueck; Startup- und Feedback-Blinken.

### Konstanten (static, nur in `.cpp`)

| Name | Wert | Bedeutung |
|------|------|-----------|
| `kButtonGpio` | 2 | Taster (in `button.h`; auch fuer Light-Sleep-Wakeup in `main`) |
| `kButtonLedPin` | 4 | LED |
| `kFactoryResetHoldMs` | 10000 | Factory Reset (Langdruck **10 s**) |
| `kShortPressMinMs` | 50 | Mindestdauer fuer Kurzdruck |

### Oeffentliche Funktionen

| Funktion | Beschreibung |
|----------|--------------|
| `buttonInit()` | `pinMode(kButtonGpio, INPUT_PULLDOWN)`, `pinMode(kButtonLedPin, OUTPUT)` |
| `buttonStartupBlink()` | 3x 200 ms an/aus (blockierend, nur beim Start); nutzt `ledOutput()` |
| `buttonEnableLedGpioHoldForLightSleep()` | `gpio_hold_en` fuer LED-Pin nach Startup (von `main` nach Blink); bei jeder LED-Aenderung zuerst `gpio_hold_dis` (`ledOutput`) |
| `buttonLoop()` | Zeitdebounce (~20 ms stabiler Pegel); `HIGH` = gedrueckt; bei **10 s** Halten ohne Loslassen: Blinkmuster, dann `resetAllSettings()`; beim Loslassen nach Kurzdruck (min. `kShortPressMinMs`, unter Factory-Hold): bei aktivem Web-Auth‑Blink **`buttonSetAuthBlinkShortPressHandler`**, sonst ggf. MQTT-Sende-/LED-Sequenz wenn Server gesetzt und nicht im AP |
| `buttonAdvanceLedSequence()` | Taktet die LED-Sequenz (2x Blink, MQTT-Publish, 500 ms Pause, 2x Blink); einfache Phasen ueber Tabelle `kLedPhaseRows`, Sonderlogik fuer `PreOff2`, `PublishTry` / `PublishRetryWait`, Ende `PostOff2` / `FailOff3` |
| `buttonDebugStatus()` | Serial: Debug-Zaehler, Button- und LED-Pegel (Aufrufrhythmus nur noch in `main`, alle 5 s) |
| `buttonIsLedTxSequenceActive()` | `true`, solange die MQTT-Sende-LED-Sequenz laeuft (von `main` fuer adaptiven Light-Sleep) |

### Implementierungsdetails

- `ledOutput(level)` -- `gpio_hold_dis` + `digitalWrite`; `ledHoldWhenIdle()` am Ende der Sequenz (`PostOff2`, `FailOff3`).
- `#include <driver/gpio.h>` fuer Hold.

### Abhaengigkeiten

- `#include "mqtt/config.h"` fuer `mqttCfgSnapshot` u.a.
- `#include "wifi/wlan.h"` fuer `resetAllSettings`, `configIsApMode`
- `#include "heart/counter.h"` fuer `heartSentCounter`, Speichern nach Publish

---

## Querverweise

- Uebersicht fuer Nutzer: [README.md](README.md)
- Ablauf und MQTT: [ARCHITECTURE.md](ARCHITECTURE.md)
- Pins: [HARDWARE.md](HARDWARE.md)
