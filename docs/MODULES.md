# Code-Referenz (Module)

Übersicht aller Quelldateien unter `src/`: öffentliche API, globale Symbole und relevante Implementierungsdetails.

---

## `main.cpp`

**Zweck:** Einstiegspunkt, Initialisierungsreihenfolge, Hauptschleife mit WiFi-Watchdog und periodischem Debug.

### Ablauf `setup()`

1. `Serial.begin(115200)`
2. `displayInit()` – SPI + E-Paper
3. `buttonInit()` – GPIO Button & LED
4. `loadMQTTConfig()` – MQTT-Werte aus NVS
5. `setupWiFi()` – WiFiManager inkl. Portal-Parameter
6. `mqttSetup()` – TLS-Client, Broker, Callback
7. `drawHeartWithNumber()` – erste Darstellung (`counter` initial 0)
8. `buttonStartupBlink()` – 3× LED-Blitz

### Ablauf `loop()`

| Aufruf | Bedeutung |
|--------|-----------|
| `buttonLoop()` | Taster entprellen, Kurz-/Langdruck |
| `checkLEDStatus()` | zeitgesteuertes LED-Aus (aktuell parallel zu blockierendem Blink weniger genutzt) |
| `mqttLoop()` | `client.loop()`, ggf. `mqttReconnect()` |
| `WiFi.reconnect()` | wenn `WiFi.status() != WL_CONNECTED` |
| `buttonDebugStatus()` | alle 5 s Serial-Status |
| `delay(5)` | kurze Pause |

---

## `config.h` / `config.cpp`

**Zweck:** Persistente MQTT-Konfiguration und WLAN-Einrichtung über **WiFiManager**; Factory Reset.

### Globale Variablen (in `config.cpp` definiert, in `config.h` deklariert)

| Symbol | Typ | Beschreibung |
|--------|-----|----------------|
| `preferences` | `Preferences` | NVS-Zugriff |
| `mqtt_server` | `char[128]` | Broker-Hostname oder IP |
| `mqtt_port` | `int` | Broker-Port (Default **8883**) |
| `mqtt_username` | `char[64]` | optional |
| `mqtt_password` | `char[64]` | optional |
| `mqtt_topic` | `char[128]` | Default `esp32/heart_counter` |

### Öffentliche Funktionen

| Funktion | Beschreibung |
|----------|--------------|
| `loadMQTTConfig()` | Liest Namespace `"mqtt"` (readonly): `server`, `port`, `user`, `pass`, `topic` |
| `saveMQTTConfig()` | Schreibt dieselben Keys |
| `setupWiFi()` | WiFiManager: Timeout **180 s**, AP-Name **`HeartESP32-Setup`**, fünf Custom-Parameter für MQTT; `setSaveParamsCallback(saveParamsFromPortal)`; bei Fehlschlag `ESP.restart()` |
| `resetAllSettings()` | `WiFiManager::resetSettings()`, `preferences` Namespace `mqtt` `clear()`, Neustart |

### Implementierungsdetails

- `safeStrCopy(dst, dstSize, src)` – begrenztes `strncpy`, immer nullterminiert.
- `saveParamsFromPortal()` – liest Werte aus `WiFiManagerParameter*`, validiert Port (1–65535, sonst 8883), ruft `saveMQTTConfig()` auf.
- Nach erfolgreichem `autoConnect` werden die globalen Parameter-Zeiger auf `nullptr` gesetzt (Lebensdauer der lokalen `WiFiManagerParameter`-Objekte).

---

## `display.h` / `display.cpp`

**Zweck:** E-Paper ansteuern und Herz mit Zählerstand zeichnen.

### Globale Symbole

| Symbol | Beschreibung |
|--------|--------------|
| `display` | `GxEPD2_3C<GxEPD2_154_Z90c, …>` – CS=15, DC=27, RST=26, BUSY=25 |
| `counter` | `int` – angezeigter und für MQTT genutzter Zähler |

### Öffentliche Funktionen

| Funktion | Beschreibung |
|----------|--------------|
| `displayInit()` | `SPI.begin(13, 12, 14, 15)`; `display.init(115200, true, 2, false)` |
| `drawHeartWithNumber()` | Vollbild-Refresh: weißer Hintergrund, rotes Herz aus zwei Kreisen, gefüllter Bereich und Linien-Trapez, schwarze Zahl (`setTextSize(4)`) unten mittig |

### Implementierungsdetails

- Zeichnung in `firstPage()` / `nextPage()`-Schleife (partial window = full window).
- Herz-Geometrie: parametrisiert um `centerX`, `centerY`, `heartSize` (siehe Quellcode für Feintuning).

---

## `mqtt.h` / `mqtt.cpp`

**Zweck:** MQTT über TLS; Verbindung halten; bei Nachricht Counter erhöhen und Display aktualisieren.

### Globale Symbole

| Symbol | Beschreibung |
|--------|--------------|
| `espClient` | `WiFiClientSecure`, `setInsecure()` |
| `client` | `PubSubClient(espClient)` |

### Öffentliche Funktionen

| Funktion | Beschreibung |
|----------|--------------|
| `mqttSetup()` | `setServer(mqtt_server, mqtt_port)`, `setCallback(mqttCallback)` |
| `mqttReconnect()` | Blockierende Schleife bis `client.connected()`: leerer Server → Log + 10 s warten; WiFi/DNS-Prüfung; `client.connect(clientId, mqtt_username, mqtt_password)`; `subscribe(mqtt_topic)` |
| `mqttLoop()` | Bei Bedarf `mqttReconnect()`, sonst `client.loop()` |

### Callback `mqttCallback`

- Baut `String` aus Payload-Bytes.
- **Ignoriert** den Topic-Namen (`(void)topic`).
- **`counter++`**, dann `drawHeartWithNumber()`.

### Implementierungsdetails

- Client-ID: `ESP32Heart-` + zufälliger Hex-Wert.
- DNS: `WiFi.hostByName(mqtt_server, serverIP)` vor Connect-Versuch.

---

## `button.h` / `button.cpp`

**Zweck:** Taster mit LED; Kurzdruck sendet MQTT; Langdruck setzt Gerät zurück; Startup- und Feedback-Blinken.

### Konstanten (static, nur in `.cpp`)

| Name | Wert | Bedeutung |
|------|------|-----------|
| `BUTTON_PIN` | 2 | Taster |
| `BUTTON_LED_PIN` | 4 | LED |
| `LONG_PRESS_MS` | 5000 | Factory Reset |
| `SHORT_PRESS_MIN_MS` | 50 | Mindestdauer für „Kurzdruck“ |

### Öffentliche Funktionen

| Funktion | Beschreibung |
|----------|--------------|
| `buttonInit()` | `pinMode(BUTTON_PIN, INPUT)`, `pinMode(BUTTON_LED_PIN, OUTPUT)` |
| `buttonStartupBlink()` | 3× 200 ms an/aus |
| `buttonLoop()` | Zustandslogik: `HIGH` = gedrückt; bei 5 s ohne Loslassen `resetAllSettings()`; beim Loslassen nach kurzem Druck `handleButtonPress()` |
| `checkLEDStatus()` | Schaltet LED aus, wenn `ledActive` und Zeit abgelaufen (für erweiterbare Timed-LED vorgesehen; Hauptblinken nutzt `delay` in `blinkLEDTwice`) |
| `buttonDebugStatus()` | Serial: Debug-Zähler, Button- und LED-Pegel |

### `handleButtonPress()` (static)

1. `blinkLEDTwice()` (blockierend mit `delay`)
2. Wenn `client.connected()`: `client.publish(mqtt_topic, String(counter).c_str())`
3. Bei Erfolg: 500 ms Pause, erneut `blinkLEDTwice()`
4. Sonst Fehlermeldung auf Serial

### Abhängigkeiten

- `#include "mqtt.h"` für `client`
- `#include "display.h"` für `counter`
- `#include "config.h"` für `mqtt_topic`, `resetAllSettings`

---

## Querverweise

- Übersicht für Nutzer: [README.md](README.md)
- Ablauf und MQTT: [ARCHITECTURE.md](ARCHITECTURE.md)
- Pins: [HARDWARE.md](HARDWARE.md)
