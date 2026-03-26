# Code-Referenz (Module)

Uebersicht aller Quelldateien unter `src/`: oeffentliche API, globale Symbole und relevante Implementierungsdetails.

---

## `main.cpp`

**Zweck:** Einstiegspunkt, Initialisierungsreihenfolge, Hauptschleife mit WiFi-Watchdog und periodischem Debug.

### Ablauf `setup()`

1. `setCpuFrequencyMhz(80)` -- niedrigere CPU-Taktfrequenz (weniger Strom)
2. `Serial.begin(115200)`
3. `displayInit()` -- SPI + E-Paper
4. `buttonInit()` -- GPIO Button & LED
5. `loadMQTTConfig()` -- MQTT-Werte aus NVS
6. `loadHeartCounter()` -- Zaehler aus NVS (Namespace `heart`)
7. `setupWiFi()` -- WiFiManager inkl. Portal-Parameter; danach `WiFi.setSleep(true)` (Modem Sleep)
8. `mqttSetup()` -- TLS-Client mit eingebautem CA-Bundle, Broker, Callback
9. `drawHeartWithNumber()` -- erste Darstellung (mit geladenem `heartCounter`); danach `display.hibernate()`
10. `buttonStartupBlink()` -- 3x LED-Blitz

### Ablauf `loop()`

| Aufruf | Bedeutung |
|--------|-----------|
| `buttonLoop()` | Taster entprellen, Kurz-/Langdruck |
| `checkLEDStatus()` | nicht-blockierende MQTT-Sende-LED-Sequenz (State Machine; millis()-overflow-sicheres Phasen-Timing) |
| `mqttLoop()` | nicht-blockierender Reconnect mit exponentiellem Backoff, dann `client.loop()` |
| `maybeSaveHeartCounter()` | Zaehler throttled (~30 s) nach NVS, wenn seit letztem Save geaendert |
| `consumeHeartRedraw()` / `drawHeartWithNumber()` | wenn nach MQTT ein Neuzeichnen angefordert wurde (ohne sofortiges `flushHeartCounterIfDirty`) |
| `WiFi.reconnect()` | bei getrenntem WLAN hoechstens alle **30 s** |
| `buttonDebugStatus()` | alle 5 s Serial-Status (nur noch ein Timer in `main`) |
| `delay(10)` | kurze Pause |

---

## `config.h` / `config.cpp`

**Zweck:** Persistente MQTT-Konfiguration und WLAN-Einrichtung ueber **WiFiManager**; Factory Reset.

### Globale Variablen (in `config.cpp` definiert, in `config.h` deklariert)

| Symbol | Typ | Beschreibung |
|--------|-----|----------------|
| `heartCounter` | `int` | Herz-Zaehler (Anzeige + MQTT); Persistenz Namespace `heart`, NVS-Key weiterhin `counter` |
| `mqtt_server` | `char[128]` | Broker-Hostname oder IP |
| `mqtt_port` | `int` | Broker-Port (Default **8883**) |
| `mqtt_username` | `char[64]` | optional |
| `mqtt_password` | `char[64]` | optional |
| `mqtt_topic_pub` | `char[128]` | Sende-Topic (Default `heart/to_b`) |
| `mqtt_topic_sub` | `char[128]` | Empfangs-Topic (Default `heart/to_a`) |

### Oeffentliche Funktionen

| Funktion | Beschreibung |
|----------|--------------|
| `loadMQTTConfig()` | Liest Namespace `"mqtt"` (readonly): `server`, `port`, `user`, `pass`, `topic_pub`, `topic_sub`; **Port** wird auf 1--65535 validiert (sonst **8883**) |
| `saveMQTTConfig()` | Schreibt dieselben Keys |
| `loadHeartCounter()` | Liest `heartCounter` aus Namespace `"heart"` (Key `counter`) |
| `saveHeartCounter()` | Schreibt aktuellen `heartCounter` nach `"heart"` |
| `maybeSaveHeartCounter()` | Schreibt nur, wenn `heartCounter` sich geaendert hat und seit letztem Schreiben mindestens **30 s** vergangen sind (weniger Flash-Verschleiss) |
| `flushHeartCounterIfDirty()` | Sofortiges NVS-Schreiben bei Dirty-`heartCounter` (z. B. vor `ESP.restart()` nach fehlgeschlagenem Portal) |
| `setupWiFi()` | WiFiManager: Timeout **180 s**, AP-Name **`HeartESP32-Setup`**, sechs Custom-Parameter fuer MQTT (Stack-Lebensdauer; Save-Callback nur waehrend `autoConnect()`); `setSaveParamsCallback(saveParamsFromPortal)`; bei Fehlschlag `flushHeartCounterIfDirty()`, dann `ESP.restart()`; nach Erfolg `WiFi.setSleep(true)` |
| `resetAllSettings()` | `WiFiManager::resetSettings()`, Namespaces `mqtt` und `heart` `clear()`, Neustart |

### Implementierungsdetails

- `Preferences preferences` ist **file-static** in `config.cpp` (kein globales Symbol in `config.h`).
- `safeStrCopy(dst, dstSize, src)` -- begrenztes `strncpy`, immer nullterminiert.
- `saveParamsFromPortal()` -- liest Werte aus `WiFiManagerParameter*`, validiert Port (1--65535, sonst 8883), ruft `saveMQTTConfig()` auf.
- `WiFiManagerParameter`-Instanzen sind **lokal auf dem Stack** in `setupWiFi()`; `g_param_*` zeigen nur waehrend `autoConnect()` darauf (Save-Callback laeuft nur dort). Nach `autoConnect` werden die globalen Zeiger auf `nullptr` gesetzt.

---

## `display.h` / `display.cpp`

**Zweck:** E-Paper ansteuern und Herz mit Zaehlerstand zeichnen.

### Globale Symbole

| Symbol | Beschreibung |
|--------|--------------|
| `display` | `GxEPD2_3C<GxEPD2_154_Z90c, ...>` -- CS=15, DC=27, RST=26, BUSY=25 |

Der Zaehlerstand kommt aus **`config.h`** (`extern int heartCounter`).

### Oeffentliche Funktionen

| Funktion | Beschreibung |
|----------|--------------|
| `displayInit()` | `SPI.begin(13, 12, 14, 15)`; `display.init(115200, true, 2, false)` |
| `drawHeartWithNumber()` | Vollbild-Refresh: weisser Hintergrund, rotes Herz aus zwei Kreisen, `fillTriangle` fuer die Spitze, gefuellter Bereich, schwarze Zahl unten mittig (`setTextSize` **2--4** je nach Stellenzahl); danach `display.hibernate()` (Controller Deep Sleep, Bild bleibt bistabil) |
| `requestHeartRedraw()` | Setzt internes Flag (nach MQTT-Empfang) |
| `consumeHeartRedraw()` | Liefert `true` einmalig wenn Neuzeichnen angefordert; loescht das Flag |

### Implementierungsdetails

- Zeichnung in `firstPage()` / `nextPage()`-Schleife (partial window = full window).
- Herz-Geometrie: `static constexpr` Konstanten im Quellcode (Feintuning dort).
- `getTextBounds()` fuer die Zahl **nach** dynamischem `setTextSize` (korrekte Zentrierung); Randpruefung fuer das Dreieck mit `display.width()` / `display.height()`.
- Zeichenfortschritt-Serial nur bei `CORE_DEBUG_LEVEL > 0`.

---

## `mqtt.h` / `mqtt.cpp`

**Zweck:** MQTT ueber TLS; Verbindung halten; bei Nachricht Counter erhoehen und Display aktualisieren.

### Kapselung

`WiFiClientSecure` und `PubSubClient` sind **nur in `mqtt.cpp`** (file-static), nicht in `mqtt.h` exportiert.

### Oeffentliche Funktionen

| Funktion | Beschreibung |
|----------|--------------|
| `mqttSetup()` | `setBufferSize(512)`, `setServer`, `setCallback`, `setKeepAlive(60)`, `setSocketTimeout(5)` (s), `setCACertBundle()` mit eingebettetem Mozilla-Bundle |
| `mqttLoop()` | Wenn nicht verbunden: Connect-Versuch wenn `millis() - lastAttempt >= backoff` (overflow-sicher); bei Connect-Fehler exponentieller Backoff 5 s bis max. 60 s; leerer Server / kein WLAN: feste Wartezeiten; bei **Uebergang** zu verbunden: Backoff zuruecksetzen; danach `client.loop()` |
| `mqttPublishHeart()` | Publiziert Payload **`heart`** auf `mqtt_topic_pub`, wenn verbunden; bis zu **5** Versuche mit `client.loop()` (PubSubClient hat kein QoS-1-Publish) |

### Callback `mqttCallback`

- Nur Payload exakt **`heart`** (5 Bytes) wird akzeptiert; alles andere wird ignoriert.
- Bei `CORE_DEBUG_LEVEL > 0`: Serial mit `Serial.write(payload, length)` (ohne `String`-Allokation).
- **Ignoriert** den Topic-Namen (`(void)topic`).
- **`heartCounter++`**, `requestHeartRedraw()`; NVS-Schreiben laeuft throttled ueber `maybeSaveHeartCounter()` in `loop()` (kein E-Paper im Callback).

### Implementierungsdetails

- Subscribe: `client.subscribe(mqtt_topic_sub, 1)` (QoS 1).
- Verbindungs-/Debug-Serial nur bei `CORE_DEBUG_LEVEL > 0`.
- Client-ID: `ESP32Heart-` + zufaelliger Hex-Wert (`snprintf`, kein Arduino-`String`).
- CA-Bundle: Eingebautes Linker-Symbol `_binary_x509_crt_bundle_start` aus `libmbedtls.a` (ESP-IDF `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE`). Kein externes Bundle noetig.
- Keine separate DNS-Vorabfrage; Aufloesung erfolgt im TLS-/TCP-Stack beim Connect.
- Ohne WLAN: nur Warte-Backoff (**kein** `WiFi.reconnect()` hier; `main` uebernimmt Reconnect).

---

## `button.h` / `button.cpp`

**Zweck:** Taster mit LED; Kurzdruck sendet MQTT; Langdruck setzt Geraet zurueck; Startup- und Feedback-Blinken.

### Konstanten (static, nur in `.cpp`)

| Name | Wert | Bedeutung |
|------|------|-----------|
| `BUTTON_PIN` | 2 | Taster |
| `BUTTON_LED_PIN` | 4 | LED |
| `LONG_PRESS_MS` | 5000 | Factory Reset |
| `SHORT_PRESS_MIN_MS` | 50 | Mindestdauer fuer Kurzdruck |

### Oeffentliche Funktionen

| Funktion | Beschreibung |
|----------|--------------|
| `buttonInit()` | `pinMode(BUTTON_PIN, INPUT_PULLDOWN)`, `pinMode(BUTTON_LED_PIN, OUTPUT)` |
| `buttonStartupBlink()` | 3x 200 ms an/aus (blockierend, nur beim Start) |
| `buttonLoop()` | Zustandslogik: `HIGH` = gedrueckt; bei 5 s ohne Loslassen `resetAllSettings()`; beim Loslassen nach kurzem Druck Start der **nicht-blockierenden** Sende-/LED-Sequenz (wenn keine Sequenz aktiv) |
| `checkLEDStatus()` | Taktet die LED-Sequenz (2x Blink, MQTT-Publish, 500 ms Pause, 2x Blink) ohne `delay()` in der Hauptschleife; Phasen mit `ledPhaseStartMs` / `ledPhaseDurationMs` (millis()-overflow-sicher) |
| `buttonDebugStatus()` | Serial: Debug-Zaehler, Button- und LED-Pegel (Aufrufrhythmus nur noch in `main`, alle 5 s) |

### Abhaengigkeiten

- `#include "mqtt.h"` fuer `mqttPublishHeart()`
- `#include "config.h"` fuer `resetAllSettings`

---

## Querverweise

- Uebersicht fuer Nutzer: [README.md](README.md)
- Ablauf und MQTT: [ARCHITECTURE.md](ARCHITECTURE.md)
- Pins: [HARDWARE.md](HARDWARE.md)
