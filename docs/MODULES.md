# Code-Referenz (Module)

Uebersicht aller Quelldateien unter `src/`: oeffentliche API, globale Symbole und relevante Implementierungsdetails.

---

## `main.cpp`

**Zweck:** Einstiegspunkt, Initialisierungsreihenfolge, Hauptschleife, adaptiver Light-Sleep.

### Ablauf `setup()`

1. CPU **80 MHz** (`setCpuFrequencyMhz` / `board_build.f_cpu` in `platformio.ini`)
2. Bluetooth aus: `btStop()`, `esp_bt_controller_mem_release(ESP_BT_MODE_BTDM)`
3. `Serial.begin(115200)` nur wenn `CORE_DEBUG_LEVEL > 0`
4. `displayInit()` -- SPI + E-Paper
5. `buttonInit()`
6. `loadMQTTConfig()` (**mqtt_config**)
7. `loadHeartCounter()` (**counter**)
8. `configLoadResetPeriodFromNvs()` (**counter**, cached reset period)
9. `setupWiFi()` (**wlan**: registriert Routen, startet `AsyncWebServer`)
10. `mqttSetup()`
11. `armLightSleepStaticWakeups()` (GPIO Taster, WiFi-Wakeup); Timer-Wakeup in `loop()`
12. Erste Zeichnung: `drawHeartWithNumber()` oder `drawSplashScreen()`
13. `buttonStartupBlink()`, `buttonEnableLedGpioHoldForLightSleep()`

### Hilfsfunktionen in `main.cpp` (file-static)

| Funktion | Beschreibung |
|----------|--------------|
| `computeLightSleepTimerUs()` | **10 ms** bei aktivem Setup-Portal, aktiver LED-Senden-Sequenz oder wenn MQTT-Backoff laeuft ( Alignment auf Restzeit bis Connect-Versuch ); sonst **2 s** Idle |
| `armLightSleepStaticWakeups()` | Einmalig: GPIO-Wakeup Taster, `esp_sleep_enable_gpio_wakeup`, `esp_sleep_enable_wifi_wakeup` |
| `armLightSleepTimerWakeup(timerUs)` | `esp_sleep_enable_timer_wakeup` bei Timerwechsel |

### Ablauf `loop()`

| Aufruf | Bedeutung |
|--------|-----------|
| `buttonLoop()`, `buttonAdvanceLedSequence()` | Taster / LED-State-Machine |
| `wlanLoop()` | (**wlan**) Captive DNS, mDNS nach GOT_IP |
| `webAdminLoop()` | MQTT-Apply, Reboot/OTA queue, `otaLoop()` |
| `maybePeriodicallyResetCounters()` | (**counter**) nur wenn nicht AP |
| `maybeResetDisplayBaselinesWhenCapped()` | (**counter**) nur wenn nicht AP: wenn Anzeige-Delta einer Seite >= 999, Baseline auf aktuellen MQTT-Zähler |
| `mqttLoop()` | MQTT + Backoff |
| `maybeSaveHeartCounter()`, `maybeSaveHeartSentCounter()` | NVS throttled (~30 s) |
| `consumeHeartRedraw()` / `drawHeartWithNumber()` | Display-Update bei MQTT |
| `buttonDebugStatus()` | nur Debug-Build, alle 5 s |
| `esp_light_sleep_start()` | kein Light-Sleep bei TLS verbunden, unkonfiguriertem MQTT-Server oder aktivem AP |

---

## `mqtt_config.h` / `mqtt_config.cpp`

**Zweck:** Nur **MQTT-Broker-Konfiguration** im NVS-Namespace `mqtt` (Defaults und Port-Normalisierung in `constants.h`).

### Globale Variablen

| Symbol | Typ | Beschreibung |
|--------|-----|--------------|
| `mqttCfg` | `MqttConfig` | `server`, `port`, `username`, `password`, `topicPub`, `topicSub` |

### Funktionen

| Funktion | Beschreibung |
|----------|--------------|
| `loadMQTTConfig()` | Lesen aus `mqtt`; Defaults fuer Topics wenn nicht gesetzt |
| `saveMQTTConfig()` | Schreiben nach `mqtt` |

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

## `wlan.h` / `wlan.cpp`

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

**Zweck:** Zugriffsschutz für die Admin-UI: Session, CSRF, `/auth`, Redirects für unauthentifizierte Requests; `webAuthLoop()`.

---

## `ota.h` / `ota.cpp`

**Zweck:** GitHub `releases/latest` (JSON `tag_name` via ArduinoJson mit Legacy-Fallback), täglicher Auto-Check (NVS `cfg`/`upd_day`), manueller Check-Button, Firmware-Install über `HTTPUpdate` (TLS + CA-Bundle).

| Funktion | Beschreibung |
|----------|--------------|
| `otaLoop()` | Auto-Update-Logik + ausstehender Download |
| `otaQueueGithubCheck()` | Manueller Versions-Vergleich |

---

## `src/web/pages.h` / `pages.cpp`

**Zweck:** HTML-Streaming (Dashboard, Wi‑Fi-Scan-JSON, MQTT-Formular, Settings, Update-Seite); gemeinsames CSS via `src/web/styles.h`, Wi‑Fi-Scan-JS via `src/web/wifi_scan_js.h`.

**Hinweis:** Die Header `styles.h` und `wifi_scan_js.h` sind eingebettete Assets; bei einem zukünftigen Build-Skript die Ausgabe nach `src/web/` schreiben.

---

## `display.h` / `display.cpp`

**Zweck:** E-Paper ansteuern und Herz mit Zaehlerstand zeichnen.

### Globale Symbole

| Symbol | Beschreibung |
|--------|--------------|
| Display-Instanz | nur in `display.cpp` (`static`), nicht exportiert -- CS=15, DC=27, RST=26, BUSY=25 |

Der Zaehlerstand und Baselines kommen aus **`counter.h`**.

### Oeffentliche Funktionen

| Funktion | Beschreibung |
|----------|--------------|
| `displayInit()` | `SPI.begin(13, 12, 14, 15)`; `display.init(...)` mit **115200** nur wenn `CORE_DEBUG_LEVEL > 0`, sonst **0** (kein `Serial.begin` durch GxEPD2 im Release) |
| `drawHeartWithNumber()` | Vollbild-Refresh: weisser Hintergrund, rotes Herz aus zwei Kreisen, `fillTriangle` fuer die Spitze, gefuellter Bereich, schwarze Zahl unten mittig (`setTextSize` **2--4** je nach Stellenzahl); danach `display.hibernate()` (Controller Deep Sleep, Bild bleibt bistabil) |
| `requestHeartRedraw()` | Setzt internes Flag (nach MQTT-Empfang) |
| `consumeHeartRedraw()` | Liefert `true` einmalig wenn Neuzeichnen angefordert; loescht das Flag |

### Implementierungsdetails

- Zeichnung in `firstPage()` / `nextPage()`-Schleife (partial window = full window).
- Herz-Geometrie: `static constexpr` Konstanten im Quellcode (Feintuning dort).
- `getTextBounds()` fuer die Zahl **nach** dynamischem `setTextSize` (korrekte Zentrierung); Randpruefung fuer das Dreieck mit `display.width()` / `display.height()`; Stellenanzahl aus `snprintf`-Rueckgabewert (kein `strlen`).
- Zeichenfortschritt-Serial nur bei `CORE_DEBUG_LEVEL > 0`.
- `g_heartRedrawPending` als **`std::atomic<bool>`** (Callback setzt Flag, `loop()` liest nach Light-Sleep).

---

## `mqtt.h` / `mqtt.cpp`

**Zweck:** MQTT ueber TLS; Verbindung halten; bei Nachricht Counter setzen und Display aktualisieren.

### Kapselung

`WiFiClientSecure` und `PubSubClient` sind **nur in `mqtt.cpp`** (file-static), nicht in `mqtt.h` exportiert.

### Oeffentliche Funktionen

| Funktion | Beschreibung |
|----------|--------------|
| `mqttSetup()` | `setBufferSize(512)` mit Pruefung des Rueckgabewerts; `setServer`, `setCallback`, `setKeepAlive(60)`, `setSocketTimeout(5)` (s), `setCACertBundle()` mit eingebettetem Mozilla-Bundle (bei sehr langen Portal-Strings ggf. Buffer in Code erhoehen) |
| `mqttLoop()` | Wenn nicht verbunden: Connect-Versuch wenn `millis() - lastAttempt >= backoff` (overflow-sicher); bei Connect-Fehler exponentieller Backoff 5 s bis max. 60 s; **leerer MQTT-Server:** Warteintervall **60 s**; kein WLAN: **5 s**; bei **Uebergang** zu verbunden: Backoff zuruecksetzen; danach `client.loop()` |
| `mqttMillisUntilNextConnectAttempt()` | Wenn nicht verbunden: verbleibende ms bis zum naechsten Connect-Versuch; **0** wenn verbunden oder Versuch faellig (fuer Light-Sleep-Timer in `main`) |
| `mqttPublishChaya()` | Publiziert `heartSentCounter + 1` als Dezimalstring (**retained**) auf `mqtt_topic_pub`, wenn verbunden; **2** Versuche laufen nicht-blockierend in `button.cpp` (LED-State-Machine, Phase `PublishRetryWait`) |

### Callback `mqttCallback`

- Payload wird als Dezimalstring geparst (max. 10 Zeichen, `strtol`, `errno == ERANGE` wird geprueft); ungueltige Payloads werden ignoriert.
- **Ignoriert** den Topic-Namen (`(void)topic`).
- `heartCounter` wird direkt auf den empfangenen Wert **gesetzt** (kein `++`); `requestHeartRedraw()` nur, wenn sich der Wert geaendert hat.
- Kein E-Paper im Callback; Persistenz des Zaehlers ueber `maybeSaveHeartCounter()` in `loop()` (throttled ~30 s).

### Implementierungsdetails

- Subscribe: `client.subscribe(mqtt_topic_sub, 1)` (QoS 1); bei Fehlschlag Debug-Meldung (`MQTT: Subscribe fehlgeschlagen.`).
- Last-Will-Topic: Puffer **140** Zeichen fuer `mqtt_topic_pub` + `"/lwt"`.
- Verbindungs-/Debug-Serial nur bei `CORE_DEBUG_LEVEL > 0`.
- Client-ID: `Chaya2MQTT-` + `esp_random()`-Hex (`snprintf`, kein Arduino-`String`).
- Connect mit **Last Will**: Topic = Sende-Topic + Suffix `/lwt`, Payload `offline`, QoS 1, retain. Nach erfolgreichem Connect wird retained `"online"` auf dasselbe LWT-Topic gepublished.
- CA-Bundle: Eingebautes Linker-Symbol `_binary_x509_crt_bundle_start` aus `libmbedtls.a` (ESP-IDF `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE`). Kein externes Bundle noetig.
- Keine separate DNS-Vorabfrage; Aufloesung erfolgt im TLS-/TCP-Stack beim Connect.
- Ohne WLAN: nur Warte-Backoff (**kein** `WiFi.reconnect()` hier; **wlan** uebernimmt STA-Reconnect).

---

## `button.h` / `button.cpp`

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
| `buttonLoop()` | Zeitdebounce (~20 ms stabiler Pegel); `HIGH` = gedrueckt; bei **10 s** Halten ohne Loslassen: Blinkmuster, dann `resetAllSettings()`; beim Loslassen nach Kurzdruck (min. `kShortPressMinMs`, unter Factory-Hold): Start der nicht-blockierenden Sende-/LED-Sequenz wenn MQTT-Server gesetzt und nicht im AP |
| `buttonAdvanceLedSequence()` | Taktet die LED-Sequenz (2x Blink, MQTT-Publish, 500 ms Pause, 2x Blink); einfache Phasen ueber Tabelle `kLedPhaseRows`, Sonderlogik fuer `PreOff2`, `PublishTry` / `PublishRetryWait`, Ende `PostOff2` / `FailOff3` |
| `buttonDebugStatus()` | Serial: Debug-Zaehler, Button- und LED-Pegel (Aufrufrhythmus nur noch in `main`, alle 5 s) |
| `buttonIsLedTxSequenceActive()` | `true`, solange die MQTT-Sende-LED-Sequenz laeuft (von `main` fuer adaptiven Light-Sleep) |

### Implementierungsdetails

- `ledOutput(level)` -- `gpio_hold_dis` + `digitalWrite`; `ledHoldWhenIdle()` am Ende der Sequenz (`PostOff2`, `FailOff3`).
- `#include <driver/gpio.h>` fuer Hold.

### Abhaengigkeiten

- `#include "mqtt_config.h"` fuer `mqttCfg`
- `#include "wlan.h"` fuer `resetAllSettings`, `configIsApMode`
- `#include "counter.h"` fuer `heartSentCounter`, Speichern nach Publish

---

## Querverweise

- Uebersicht fuer Nutzer: [README.md](README.md)
- Ablauf und MQTT: [ARCHITECTURE.md](ARCHITECTURE.md)
- Pins: [HARDWARE.md](HARDWARE.md)
