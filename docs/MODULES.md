# Code-Referenz (Module)

Uebersicht aller Quelldateien unter `src/`: oeffentliche API, globale Symbole und relevante Implementierungsdetails.

---

## `main.cpp`

**Zweck:** Einstiegspunkt, Initialisierungsreihenfolge, Hauptschleife mit WiFi-Watchdog und periodischem Debug.

### Ablauf `setup()`

1. `Serial.begin(115200)`
2. `displayInit()` -- SPI + E-Paper
3. `buttonInit()` -- GPIO Button & LED
4. `loadMQTTConfig()` -- MQTT-Werte aus NVS
5. `loadHeartCounter()` -- Zaehler aus NVS (Namespace `heart`)
6. `setupWiFi()` -- WiFiManager inkl. Portal-Parameter
7. `mqttSetup()` -- TLS-Client, Broker, Callback
8. `drawHeartWithNumber()` -- erste Darstellung (mit geladenem `counter`)
9. `buttonStartupBlink()` -- 3x LED-Blitz

### Ablauf `loop()`

| Aufruf | Bedeutung |
|--------|-----------|
| `buttonLoop()` | Taster entprellen, Kurz-/Langdruck |
| `checkLEDStatus()` | nicht-blockierende MQTT-Sende-LED-Sequenz (State Machine) |
| `mqttLoop()` | nicht-blockierender Reconnect mit Backoff, dann `client.loop()` |
| `consumeHeartRedraw()` / `drawHeartWithNumber()` | wenn nach MQTT ein Neuzeichnen angefordert wurde |
| `WiFi.reconnect()` | bei getrenntem WLAN hoechstens alle **30 s** |
| `buttonDebugStatus()` | alle 5 s Serial-Status (nur noch ein Timer in `main`) |
| `delay(5)` | kurze Pause |

---

## `config.h` / `config.cpp`

**Zweck:** Persistente MQTT-Konfiguration und WLAN-Einrichtung ueber **WiFiManager**; Factory Reset.

### Globale Variablen (in `config.cpp` definiert, in `config.h` deklariert)

| Symbol | Typ | Beschreibung |
|--------|-----|----------------|
| `preferences` | `Preferences` | NVS-Zugriff |
| `mqtt_server` | `char[128]` | Broker-Hostname oder IP |
| `mqtt_port` | `int` | Broker-Port (Default **8883**) |
| `mqtt_username` | `char[64]` | optional |
| `mqtt_password` | `char[64]` | optional |
| `mqtt_topic_pub` | `char[128]` | Sende-Topic (Default `heart/to_b`) |
| `mqtt_topic_sub` | `char[128]` | Empfangs-Topic (Default `heart/to_a`) |

### Oeffentliche Funktionen

| Funktion | Beschreibung |
|----------|--------------|
| `loadMQTTConfig()` | Liest Namespace `"mqtt"` (readonly): `server`, `port`, `user`, `pass`, `topic_pub`, `topic_sub` |
| `saveMQTTConfig()` | Schreibt dieselben Keys |
| `loadHeartCounter()` | Liest `counter` aus Namespace `"heart"` (Key `counter`) |
| `saveHeartCounter()` | Schreibt aktuellen `counter` nach `"heart"` |
| `setupWiFi()` | WiFiManager: Timeout **180 s**, AP-Name **`HeartESP32-Setup`**, sechs Custom-Parameter fuer MQTT (inkl. Sende-/Empfangs-Topic); `setSaveParamsCallback(saveParamsFromPortal)`; bei Fehlschlag `ESP.restart()` |
| `resetAllSettings()` | `WiFiManager::resetSettings()`, Namespaces `mqtt` und `heart` `clear()`, Neustart |

### Implementierungsdetails

- `safeStrCopy(dst, dstSize, src)` -- begrenztes `strncpy`, immer nullterminiert.
- `saveParamsFromPortal()` -- liest Werte aus `WiFiManagerParameter*`, validiert Port (1--65535, sonst 8883), ruft `saveMQTTConfig()` auf.
- Nach erfolgreichem `autoConnect` werden die globalen Parameter-Zeiger auf `nullptr` gesetzt (Lebensdauer der lokalen `WiFiManagerParameter`-Objekte).

---

## `display.h` / `display.cpp`

**Zweck:** E-Paper ansteuern und Herz mit Zaehlerstand zeichnen.

### Globale Symbole

| Symbol | Beschreibung |
|--------|--------------|
| `display` | `GxEPD2_3C<GxEPD2_154_Z90c, ...>` -- CS=15, DC=27, RST=26, BUSY=25 |
| `counter` | `int` -- angezeigter und fuer MQTT genutzter Zaehler |

### Oeffentliche Funktionen

| Funktion | Beschreibung |
|----------|--------------|
| `displayInit()` | `SPI.begin(13, 12, 14, 15)`; `display.init(115200, true, 2, false)` |
| `drawHeartWithNumber()` | Vollbild-Refresh: weisser Hintergrund, rotes Herz aus zwei Kreisen, gefuellter Bereich und Linien-Trapez, schwarze Zahl (`setTextSize(4)`) unten mittig |
| `requestHeartRedraw()` | Setzt internes Flag (nach MQTT-Empfang) |
| `consumeHeartRedraw()` | Liefert `true` einmalig wenn Neuzeichnen angefordert; loescht das Flag |

### Implementierungsdetails

- Zeichnung in `firstPage()` / `nextPage()`-Schleife (partial window = full window).
- Herz-Geometrie: parametrisiert um `centerX`, `centerY`, `heartSize` (siehe Quellcode fuer Feintuning).
- `getTextBounds()` fuer die Zahl **nach** `setTextSize(4)` (korrekte Zentrierung); Randpruefung mit `display.width()` / `display.height()`.

---

## `mqtt.h` / `mqtt.cpp`

**Zweck:** MQTT ueber TLS; Verbindung halten; bei Nachricht Counter erhoehen und Display aktualisieren.

### Globale Symbole

| Symbol | Beschreibung |
|--------|--------------|
| `espClient` | `WiFiClientSecure`, `setInsecure()` |
| `client` | `PubSubClient(espClient)` |

### Oeffentliche Funktionen

| Funktion | Beschreibung |
|----------|--------------|
| `mqttSetup()` | `setServer(mqtt_server, mqtt_port)`, `setCallback(mqttCallback)` |
| `mqttLoop()` | Wenn nicht verbunden: ein Connect-Versuch pro Backoff-Intervall (5 s / 10 s je nach Fehlerfall), dann `client.loop()` |

### Callback `mqttCallback`

- Baut `String` aus Payload-Bytes (`reserve(length)`).
- **Ignoriert** den Topic-Namen (`(void)topic`).
- **`counter++`**, `saveHeartCounter()`, `requestHeartRedraw()` (kein E-Paper im Callback).

### Implementierungsdetails

- Client-ID: `ESP32Heart-` + zufaelliger Hex-Wert.
- DNS: `WiFi.hostByName(mqtt_server, serverIP)` vor Connect-Versuch.

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
| `checkLEDStatus()` | Taktet die LED-Sequenz (2x Blink, MQTT-Publish, 500 ms Pause, 2x Blink) ohne `delay()` in der Hauptschleife |
| `buttonDebugStatus()` | Serial: Debug-Zaehler, Button- und LED-Pegel (Aufrufrhythmus nur noch in `main`, alle 5 s) |

### Abhaengigkeiten

- `#include "mqtt.h"` fuer `client`
- `#include "display.h"` fuer `counter`
- `#include "config.h"` fuer `mqtt_topic_pub`, `resetAllSettings`

---

## Querverweise

- Uebersicht fuer Nutzer: [README.md](README.md)
- Ablauf und MQTT: [ARCHITECTURE.md](ARCHITECTURE.md)
- Pins: [HARDWARE.md](HARDWARE.md)
