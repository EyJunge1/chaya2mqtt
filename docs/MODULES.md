# Code-Referenz (Module)

Uebersicht aller Quelldateien unter `src/`: oeffentliche API, globale Symbole und relevante Implementierungsdetails.

---

## `main.cpp`

**Zweck:** Einstiegspunkt, Initialisierungsreihenfolge, Hauptschleife mit WiFi-Watchdog und periodischem Debug.

### Ablauf `setup()`

1. CPU **80 MHz** ueber `setCpuFrequencyMhz(80)` / `board_build.f_cpu` in `platformio.ini` (weniger Strom als Default 240 MHz)
2. **Bluetooth aus:** `btStop()`, `esp_bt_controller_mem_release(ESP_BT_MODE_BTDM)` (weniger RAM/Strom)
3. `Serial.begin(115200)` nur wenn `CORE_DEBUG_LEVEL > 0` (Release-Build ohne Serial-Init)
4. `displayInit()` -- SPI + E-Paper
5. `buttonInit()` -- GPIO Button & LED
6. `loadMQTTConfig()` -- MQTT-Werte aus NVS
7. `loadHeartCounter()` -- Zaehler aus NVS (Namespace `chaya`)
8. `setupWiFi()` -- WiFiManager inkl. Portal-Menue (**MQTT Settings** nach `/param`, MQTT-Felder auf eigener Parameter-Seite); danach `WiFi.setSleep(true)` und `esp_wifi_set_ps(WIFI_PS_MAX_MODEM)` (aggressiver Modem-Sleep)
9. `mqttSetup()` -- TLS-Client mit eingebautem CA-Bundle, Broker, Callback
10. `armLightSleepStaticWakeups()` einmalig (GPIO Taster HIGH, **WiFi-Wakeup**); Timer-Wakeup wird erst in `loop()` gesetzt (`armLightSleepTimerWakeup`)
11. `drawHeartWithNumber()` -- erste Darstellung (mit geladenem `heartCounter`); danach `display.hibernate()`
12. `buttonStartupBlink()` -- 3x LED-Blitz; danach `buttonEnableLedGpioHoldForLightSleep()` -- **GPIO-Hold** fuer LED-Pin (stabiler Pegel im Light-Sleep)

### Hilfsfunktionen in `main.cpp` (file-static)

| Funktion | Beschreibung |
|----------|--------------|
| `computeLightSleepTimerUs()` | **10 ms** bei aktiver LED-Sequenz; **100 ms** bei ausstehendem WiFi-Hard-Reconnect; sonst **min(Rest-MQTT-Backoff, 15 s)** (mind. 10 ms) bzw. **15 s** Idle; MQTT via `mqttMillisUntilNextConnectAttempt()` |
| `armLightSleepStaticWakeups()` | Einmalig: GPIO-Wakeup Taster, `esp_sleep_enable_gpio_wakeup`, `esp_sleep_enable_wifi_wakeup` |
| `armLightSleepTimerWakeup(timerUs)` | Nur `esp_sleep_enable_timer_wakeup` (bei Timerwechsel in `loop()`) |

### Ablauf `loop()`

| Aufruf | Bedeutung |
|--------|-----------|
| `buttonLoop()` | Taster entprellen, Kurz-/Langdruck |
| `buttonAdvanceLedSequence()` | nicht-blockierende MQTT-Sende-LED-Sequenz (State Machine; millis()-overflow-sicheres Phasen-Timing) |
| `mqttLoop()` | nicht-blockierender Reconnect mit exponentiellem Backoff, dann `client.loop()` |
| `maybeSaveHeartCounter()` | Zaehler throttled (~30 s) nach NVS, wenn seit letztem Save geaendert |
| `consumeHeartRedraw()` / `drawHeartWithNumber()` | bei MQTT-Neuzeichnung; Zaehler-Persistenz nur noch throttled ueber `maybeSaveHeartCounter()` (~30 s) |
| `WiFi.reconnect()` / Fallback | bei getrenntem WLAN: bis zu **3x** `reconnect()`, danach `disconnect` und in einer **folgenden** Iteration (nach **>= 100 ms** ohne `delay`) `WiFi.begin()`; hoechstens alle **30 s** (plus sofort beim ersten Verlust) |
| `buttonDebugStatus()` | alle 5 s Serial-Status (nur noch ein Timer in `main`, nur bei `CORE_DEBUG_LEVEL > 0`) |
| `armLightSleepTimerWakeup` / Timer-Neuarmierung | Wenn sich `computeLightSleepTimerUs()` gegenueber letzter Iteration aendert, nur den Timer neu setzen (GPIO/WiFi bleiben) |
| `esp_light_sleep_start()` | Light-Sleep (adaptiver Timer + GPIO-Wakeup Taster + **WiFi-Wakeup**) |

---

## `config.h` / `config.cpp`

**Zweck:** Persistente MQTT-Konfiguration und WLAN-Einrichtung ueber **MycilaESPConnect** (Captive Portal); Factory Reset; gemeinsamer **AsyncWebServer** mit `web_admin` (siehe unten).

### Globale Variablen (in `config.cpp` definiert, in `config.h` deklariert)

| Symbol | Typ | Beschreibung |
|--------|-----|----------------|
| `heartCounter` | `int` | Herz-Zähler (Anzeige + MQTT); Persistenz Namespace `chaya`, NVS-Key `counter` |
| `mqttCfg` | `MqttConfig` | Broker, Port, User, Pass, Pub/Sub-Topics (Felder wie `server`, `port`, …) |

### Oeffentliche Funktionen

| Funktion | Beschreibung |
|----------|--------------|
| `loadMQTTConfig()` | `preferences.begin` mit Fehlerpruefung; Liest Namespace `"mqtt"` (readonly) ohne temporaere `String`-Heap-Objekte (`getString` in feste Buffer); **Port** 1--65535 (sonst **8883**); Default-Topics wenn Key fehlt/leer |
| `saveMQTTConfig()` | Schreibt dieselben Keys; `begin` mit Fehlerpruefung |
| `loadHeartCounter()` | Liest `heartCounter` aus Namespace `"chaya"` (Key `counter`); `begin` mit Fehlerpruefung |
| `saveHeartCounter()` | Schreibt aktuellen `heartCounter` nach `"chaya"`; `begin` mit Fehlerpruefung; `true` bei Erfolg |
| `maybeSaveHeartCounter()` | Schreibt nur, wenn `heartCounter` sich geaendert hat und seit letztem Schreiben mindestens **30 s** vergangen sind (weniger Flash-Verschleiss) |
| `flushHeartCounterIfDirty()` | Sofortiges NVS-Schreiben bei Dirty-`heartCounter` (vor `ESP.restart()` / Factory-Reset, nicht nach jedem Display-Redraw) |
| `setupWiFi()` | WiFiManager: Timeout **180 s**, AP-Name **`chaya2mqtt-Setup`**, Portal-Titel **chaya2mqtt Setup**; Menue per `setMenu`: **wifi**, **param**, **info**, **update**, **exit** (Custom-Parameter auf eigener Seite `/param`, da **param** im Menue laut WM2 `_paramsInWifi=false` setzt); `setCustomHeadElement`: benennt den `/param`-Button und die Parameter-Seite per kleinem Script in **MQTT Settings** um; sechs MQTT-`WiFiManagerParameter` (Stack-Lebensdauer; Save-Callback nur waehrend `autoConnect()`); `setSaveParamsCallback(saveParamsFromPortal)`; bei Fehlschlag `flushHeartCounterIfDirty()`, **delay(500)**, dann `ESP.restart()`; nach Erfolg `WiFi.setSleep(true)` und `esp_wifi_set_ps(WIFI_PS_MAX_MODEM)` |
| `resetAllSettings()` | `WiFiManager::resetSettings()`, Namespace `mqtt` `clear()` (mit `begin`-Fehlerpruefung); **Zähler** (Namespace `chaya`) bleibt erhalten; vor Neustart `flushHeartCounterIfDirty()` |

### Implementierungsdetails

- `Preferences preferences` ist **file-static** in `config.cpp` (kein globales Symbol in `config.h`).
- `safeStrCopy(dst, dstSize, src)` -- begrenztes `strncpy`, immer nullterminiert.
- `saveParamsFromPortal()` -- liest Werte aus `WiFiManagerParameter*`, validiert Port (1--65535, sonst 8883), ruft `saveMQTTConfig()` auf.
- `WiFiManagerParameter`-Instanzen sind **lokal auf dem Stack** in `setupWiFi()`; `g_param_*` zeigen nur waehrend `autoConnect()` darauf (Save-Callback laeuft nur dort). Nach `autoConnect` werden die globalen Zeiger auf `nullptr` gesetzt.
- Portal-Startseite: Der WM-Standardbutton fuer `/param` wird per `setCustomHeadElement`/Script von **Setup** zu **MQTT Settings** umbenannt (Kunden muessen den URL-Pfad `/param` nicht kennen).

---

## `web_admin.h` / `web_admin.cpp`

**Zweck:** **HTTP-Oberfläche** für WLAN-Verbindung, Dashboard, Firmware-Update und MQTT-Konfiguration (`/mqtt` GET/POST). Nutzt dieselbe `AsyncWebServer`-Instanz (Port 80) wie das Captive Portal in **config** (`webAdminWebServer()`).

| Funktion | Kurzbeschreibung |
|----------|------------------|
| `webAdminWebServer()` | Referenz auf den gemeinsamen `AsyncWebServer` (Singleton) |
| `webAdminRegisterRoutes()` | Routen einmal registrieren (vor `begin()` auf dem Server) |
| `webAdminLoop()` | Ausstehende MQTT-Änderungen anwenden, Reboot/OTA/WiFi-Anfragen aus Request-Handlern verarbeiten |

---

## `display.h` / `display.cpp`

**Zweck:** E-Paper ansteuern und Herz mit Zaehlerstand zeichnen.

### Globale Symbole

| Symbol | Beschreibung |
|--------|--------------|
| Display-Instanz | nur in `display.cpp` (`static`), nicht exportiert -- CS=15, DC=27, RST=26, BUSY=25 |

Der Zaehlerstand kommt aus **`config.h`** (`extern int heartCounter`).

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
- Ohne WLAN: nur Warte-Backoff (**kein** `WiFi.reconnect()` hier; `main` uebernimmt Reconnect).

---

## `button.h` / `button.cpp`

**Zweck:** Taster mit LED; Kurzdruck sendet MQTT; Langdruck setzt Geraet zurueck; Startup- und Feedback-Blinken.

### Konstanten (static, nur in `.cpp`)

| Name | Wert | Bedeutung |
|------|------|-----------|
| `kButtonGpio` | 2 | Taster (in `button.h`; auch fuer Light-Sleep-Wakeup in `main`) |
| `kButtonLedPin` | 4 | LED |
| `kLongPressMs` | 5000 | Factory Reset |
| `kShortPressMinMs` | 50 | Mindestdauer fuer Kurzdruck |

### Oeffentliche Funktionen

| Funktion | Beschreibung |
|----------|--------------|
| `buttonInit()` | `pinMode(kButtonGpio, INPUT_PULLDOWN)`, `pinMode(kButtonLedPin, OUTPUT)` |
| `buttonStartupBlink()` | 3x 200 ms an/aus (blockierend, nur beim Start); nutzt `ledOutput()` |
| `buttonEnableLedGpioHoldForLightSleep()` | `gpio_hold_en` fuer LED-Pin nach Startup (von `main` nach Blink); bei jeder LED-Aenderung zuerst `gpio_hold_dis` (`ledOutput`) |
| `buttonLoop()` | Zeitdebounce (~20 ms stabiler Pegel); Zustandslogik: `HIGH` = gedrueckt; bei 5 s ohne Loslassen Blinkmuster, dann `resetAllSettings()`; beim Loslassen nach kurzem Druck Start der **nicht-blockierenden** Sende-/LED-Sequenz (wenn keine Sequenz aktiv) |
| `buttonAdvanceLedSequence()` | Taktet die LED-Sequenz (2x Blink, MQTT-Publish, 500 ms Pause, 2x Blink); einfache Phasen ueber Tabelle `kLedPhaseRows`, Sonderlogik fuer `PreOff2`, `PublishTry` / `PublishRetryWait`, Ende `PostOff2` / `FailOff3` |
| `buttonDebugStatus()` | Serial: Debug-Zaehler, Button- und LED-Pegel (Aufrufrhythmus nur noch in `main`, alle 5 s) |
| `buttonIsLedTxSequenceActive()` | `true`, solange die MQTT-Sende-LED-Sequenz laeuft (von `main` fuer adaptiven Light-Sleep) |

### Implementierungsdetails

- `ledOutput(level)` -- `gpio_hold_dis` + `digitalWrite`; `ledHoldWhenIdle()` am Ende der Sequenz (`PostOff2`, `FailOff3`).
- `#include <driver/gpio.h>` fuer Hold.

### Abhaengigkeiten

- `#include "mqtt.h"` fuer `mqttPublishChaya()`
- `#include "config.h"` fuer `resetAllSettings`

---

## Querverweise

- Uebersicht fuer Nutzer: [README.md](README.md)
- Ablauf und MQTT: [ARCHITECTURE.md](ARCHITECTURE.md)
- Pins: [HARDWARE.md](HARDWARE.md)
