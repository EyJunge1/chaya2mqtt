# Audit-Prompt: Race Conditions und Fehler (Multi-Agent)

Eine Datei für alles. Sechs Agents parallel starten; jeder bekommt den **gemeinsamen Block** plus **genau einen Agent-Block**. Danach ein Merge-Chat mit dem Merge-Block.

| Agent | Scope | ID-Präfix |
|-------|--------|-----------|
| 1 | MQTT, Publish/ACK, Heart-Counter, MQTT-Config | `RC-MQTT-` / `BUG-MQTT-` |
| 2 | WiFi, Network-Task, Recovery, NetCmd, EPD-Fenster | `RC-NET-` / `BUG-NET-` |
| 3 | Web-Admin, HTTP-Routen, SSE, deferred Work | `RC-WEB-` / `BUG-WEB-` |
| 4 | Display, Audio, Button, LED, Soft-off | `RC-UI-` / `BUG-UI-` |
| 5 | OTA, NVS, Watchdog, Init, Factory-Reset | `RC-LIFE-` / `BUG-LIFE-` |
| 6 | Frontend, Mock, Flasher, OpenAPI/AsyncAPI | `RC-FE-` / `BUG-FE-` |
| Merge | Sechs Reports → `audit/FINDINGS.md` | IDs der Agents behalten |

Nicht denselben Agent-Block zwei Chats geben. An Scope-Grenzen eine Zeile „an Agent N“ plus `Datei:Symbol`, nicht tief in fremden Code.

`audit/FINDINGS.md` ist nur Ausgabe und darf fehlen. Der lokale Code ist die Wahrheit — gefixte Stellen sind keine Findings mehr. Trotzdem: wenige echte Bugs, keine lange Liste.

---

## Gemeinsamer Block (jeden Agent mitgeben)

Du bist ein strenger Code-Reviewer für **Chaya2MQTT**. Lies den tatsächlichen Code. Docs (`docs/ARCHITECTURE.md`, `docs/MODULES.md`, `docs/MQTT.md`, `docs/WEB_ADMIN.md`, `docs/OTA.md`, `docs/CONFIGURATION.md`) sind Kontext, nicht Beweis.

### Projekt

ESP32-S3-Firmware, kein Arduino-`loop`: `setup()` startet Tasks, `loop()` macht `vTaskDelete`. Tasks auf Core 1: network, button, app, ota, display, audio. AsyncTCP auf Core 0. `esp_mqtt_client` hat eigenen Task. Display-Task hat **kein** TWDT (E-Ink bis ~20 s).

Queues: `g_netCmdQueue`, `g_displayCmdQueue`, `g_audioCmdQueue`.

Lock-Order (nie umkehren, `src/mqtt/mqtt.h`):

1. `g_chayaPublishMutex`
2. `g_mqttClientMutex`
3. optional `g_heartDebounceMutex`

Weitere: `g_nvsMutex`, `g_wifiTestMutex`, `g_wifiApiMutex`, `s_mqttCfgMutex`, TLS-Bundle-Mutex.

Web-Handler dürfen nicht blockieren: Atomics/Flags → App-Task (`webAdminLoop`) oder `NetCmd`. Nur die Display-Task darf SPI/EPD anfassen.

Chaya-Publish: Network-Task startet QoS-1; PUBACK / Disconnect / 5-s-Timeout schließen den Async-Pfad. Counter/NVS/Audio/Display **genau einmal** bei matching PUBACK. `network_task.cpp` pollt `mqttChayaPublishAsyncIsPending()` jeden Tick — ein `netCmdTrySend(ChayaPublish)`-Drop ist kein Lost-Send.

### Warum so viele Findings entstehen

Der Code ist in den Hotspots bewusst gegattet. Ein Agent, der „eine Liste liefern muss“, baut Interleavings aus NVS-voll, zwei Tabs, ms-Fenstern und HIL. Das sind keine Alltags-Bugs. **0 Findings in deinem Scope ist eine gute Antwort.** Max. **2 Findings** pro Agent. Lieber keines als ein weiches.

### Was zählt (alle müssen gelten)

1. Der Fehlerpfad steht **jetzt** im Code.
2. Ein normaler User kann ihn auslösen: Taste, ein Tab, Speichern, Unpair, Soft-off, OTA, WLAN-Save — nicht „zwei Tabs + NVS voll + ms + HIL“.
3. Du würdest den Fix mergen.
4. Die Auswirkung ist ohne HIL zwingend: verlorenes/doppeltes Herz, Send dauerhaft tot, Gerät offline ohne AP, Latch schneidet nicht, Watchdog, Brick, Restart-Schleife, `200 saved_rebooting` ohne Reboot.
5. Es ist kein dokumentiertes Design (unten).

Sonst: eine Zeile unter „ohne Befund“ oder HIL-Liste. **Kein Finding.**

### Harte Regeln

- Nur echte Issues mit Codebeleg (Datei, Symbol, Zeilen, Zitat).
- Keine Style-Hinweise, keine FreeRTOS-Lehrtexte, keine Spekulation.
- Kein Hardening-Finding, kein „Latent race“-Finding. Klasse nur **Bug**.
- Dokumentierte Designs nicht als Bug: Core-1-Pinning; Display ohne TWDT; Lock-Order; deferred Web-Work; EPD-Low-Interference (Poll 50/250 ms, kein Lost-Wakeup); QUAL-04 Settings-Pending bleibt bei NVS-Fail und wird retried; `GET /api/mqtt` = aktive Config + `applyPending` (`docs/MQTT.md`); OTA-Force macht „soft connect only“ statt disconnect+begin; LED `PublishTry` wartet auf erwarteten Reconnect; Factory flushed Hearts absichtlich nicht; SSE-Payloads werden kopiert, Erfolg nur `ENQUEUED`.
- Check-then-act nur mit Alltags-Trigger, nicht zwei Tabs / Epoch-ms.
- `s_contentAllowed` startet `false`, `loadHeartCounter` liegt davor — kein Boot-Torn-Snapshot.
- Severity nur High / Medium. **High** nur bei Brick, Gerät unerreichbar, Zählerverlust, Watchdog, totem Heart-Send im normalen Pfad. Kein High für „OTA könnte abreißen (HIL)“ oder NVS-voll.
- Bleib in deinem Scope.
- Sprache: Deutsch.

### Was du systematisch suchst (im eigenen Scope)

Shared State, der im Alltag reißt: nicht-atomare Globals über Tasks/ISR/MQTT-Event/HTTP; Lost-Update ohne Fallback.

Locks: Order-Verletzung, Deadlock, Mutex in ISR/MQTT-Event, fehlendes Unlock, Lock im Callback der schon unter Lock läuft.

Queues: wirklich verlorene `FactoryReset` / Settings **ohne** Flag-/Versions-Fallback. `GOT_IP`/`Reconnect`/`ChayaPublish` haben Fallbacks — nur melden, wenn der Fallback **jetzt** fehlt.

Lifetime: JSON-Buffer nach Handler-Return; MQTT-Client-Destroy aus dem Event-Task.

Weitere: UB, Host-Allowlist / AP-STA-Gates / OTA-URL-Allowlist gebrochen, SoftAP-PSK in API/Logs.

### Ausgabe

Zuerst Kurzfassung. Nichts gefunden: **„Keine Findings.“** und aufhören. Keine drei „kritischsten Punkte“ erfinden.

Je Finding:

- ID (dein Präfix + Nummer; bei 01 anfangen ist ok)
- Severity: High / Medium
- Klasse: Bug
- Typ: Race / Deadlock / Lost-Update / Lifetime / Logic / Contract
- Ort: `Datei:Symbol` und zweite Task/Callback
- Verletzte Invariante
- Fehlerpfad mit Alltags-Trigger (schrittweise)
- Auswirkung (user-sichtbar, ohne HIL)
- Warum es kein dokumentiertes Design ist (ein Satz)
- Fix: minimal, Repo-Stil
- Tests: bestehender `test/`/`sim/`-Fall oder fehlender Fall
- Codezitat mit Zeilen

Am Ende: geprüfte Hotspots ohne Befund; HIL-Fragen (keine Findings).

Keine allgemeine Abhandlung. Beginne jetzt.

---

## Agent 1 — MQTT / Heart

**Scope:** `src/mqtt/`, `src/heart/`, `src/button/button_actions.*`, Chaya-Send in `src/led/led.cpp`, Tests `test/test_mqtt/`, relevante Teile von `test/test_device_sim/`, `sim/fake_mqtt_transport.h`.

**Lesen:** `mqtt.h`, `mqtt_internal.h`, `mqtt_publish.cpp`, `mqtt_publish_ack.h`, `mqtt_events.cpp`, `mqtt_client.cpp`, `mqtt_reconnect.cpp`, `mqtt/config.cpp`, `mqtt/config.h`, `mqtt_timing.h`, `heart/counter.cpp`, `counter.h`, `counter_internal.h`, `counter_nvs.cpp`, `counter_sync.cpp`.

**Interleavings:**

- Button-Send || Web-Send || MQTT-PUBACK || Disconnect || 5-s-Timeout
- doppelter PUBACK, late ACK nach Timeout, ACK für falsche Generation/msg-id
- `heartSentCounterApplyAfterSuccessfulPublish` genau einmal
- RX `heartCounterStoreFromRemote` || TX || NVS-Debounce || Baseline-Reset
- Settings-Apply / Unpair mitten im Publish — nur wenn Heart-Ready / Fail **jetzt** fehlt
- Retain + QoS-1: Echo des eigenen Publishes auf Subscribe-Topic
- Lock-Order `g_chayaPublishMutex` → `g_mqttClientMutex` → `g_heartDebounceMutex`; Mutex im MQTT-Event-Handler
- `mqttBeginSettingsApply` / `mqttEndSettingsApply` / `mqttPublishBlocked`
- Destroy des Clients nicht aus dem MQTT-Event-Task

Nicht tief in WiFi-Events, HTTP-Routen oder Display-SPI.

---

## Agent 2 — WiFi / Network

**Scope:** `src/network/`, `src/wifi/`, `src/async/event_types.h`, `src/async/queue_coalesce_pure.h`, `src/async/task_handles.*`, `src/async/web_server_hooks.h`.

**Lesen:** `network_task.cpp`, `wlan.cpp`, `wlan_events.cpp`, `wlan_boot.cpp`, `wlan_reset.cpp`, `wlan_recovery.cpp`, `wlan_nvs.cpp`, `wlan_scan.cpp`, `wifi/test.cpp`, `wlan_internal.h`, `wlan_soft_reconnect.h`.

**Interleavings:**

- `netCmdTrySend` Queue voll: Fallback **jetzt** weg? (`GOT_IP`/`Reconnect`-Flags und Chaya-Pending-Poll sind der Soll-Stand)
- Unlock-Leak / Lock-Order `g_wifiApiMutex` / `g_wifiTestMutex`
- WiFi-Event macht Display/MQTT/NVS statt Atomics+`NetCmd`
- Recovery-Zähler: Claim vs. Write-Gate — nur wenn `rec_rst` **jetzt** nicht persistiert

Nicht: OTA-`STA.connect()` als Brick (absichtlich soft-only, Impact HIL). Nicht: Scan+Reconnect im selben Tick als Assoc-Drop (HIL).

Nicht tief in MQTT-Publish-ACK-Logik oder Frontend.

---

## Agent 3 — Web / SSE

**Scope:** `src/web/`, `src/async/app_task.cpp` (`webAdminLoop`, SSE-Tick), `src/async/sse_dirty.*`, `src/web/json_payloads.h`.

**Lesen:** `admin.cpp`, `admin_globals.*`, `events.cpp`, `deferred_reboot.cpp`, `web_utils.cpp`, `web_middleware.cpp`, `host_validate.h`, alle `routes/admin_routes_api_*.cpp`, `admin_routes_captive.cpp`, `admin_routes_spa.cpp`.

**Interleavings:**

- HTTP-Handler blockieren — neuer Pfad, nicht die bekannten Flags
- JSON-Buffer-Lifetime nach Handler-Return
- Host-Allowlist / AP-STA-Gates gebrochen; SoftAP-PSK in JSON/Logs
- SSE Use-after-free belegen (nicht die Library vermuten)

Nicht: `GET /api/mqtt` liefert aktiv statt Pending. Nicht: Settings-Pending bleibt bei NVS-Fail (QUAL-04).

Grenzfall MQTT-Apply-Inhalt → Agent 1; WiFi-Save-Inhalt → Agent 2; OTA-Queue → Agent 5. Nur die Web-Seite prüfen.

---

## Agent 4 — Display / Input

**Scope:** `src/display/`, `src/audio/`, `src/button/`, `src/led/`, `src/battery/` (Soft-off/Sleep), `src/hw/`.

**Lesen:** `display_task.cpp`, `display.cpp`, `draw.cpp`, `display_task_internal.h`, `audio.cpp`, `button_input.cpp`, `button_actions.cpp`, `led.cpp`, `battery.cpp`.

**Interleavings:**

- Nur Display-Task darf SPI/EPD; alle anderen nur `displayRequest`
- Heart-Coalescing: ein pending Heart; Splash/PowerOff-Priorität; Heart-Paints nach Unpair verworfen; Content-Heart no-op bis `mqttCfgIsHeartReady()`
- Audio: pending TX/RX bei Queue-Overflow, keine ungebundene Backlog, keine Doppel-Clicks pro Drain
- Button-ISR: kein Mutex, nur Notify/Queue-from-ISR; Debounce; `chayaRequestSend` gleicher Pfad wie Web
- LED-Priorität: MQTT-TX > Pattern > Refresh-Pulse > Idle; `ledIsTxSendBusy` blockt zweiten Send
- Soft-off: Latch nach 15 s bei gehaltenem PWR — nur wenn der Cut **jetzt** fehlt
- LED dauerhaft `Busy` nur wenn Connect unmöglich ist und kein Fail kommt (Reconnect-Warten ist Design)
- Display-Task ohne TWDT: andere Tasks dürfen nicht auf E-Ink warten und dabei TWDT riskieren
- Cap 999, Baseline-Roll nur soweit Display-Snapshot betroffen (Counter-Persistenz → Agent 1/5)

Nicht tief in MQTT-ACK oder NVS-Debounce.

---

## Agent 5 — OTA / NVS / Lifecycle

**Scope:** `src/ota/`, `src/config/`, `src/async/system_lifecycle.*`, `src/async/task_handles.*`, `src/diag/`, `src/tls/`, `src/main.cpp`, `src/identity/`.

**Lesen:** `ota.cpp`, `ota_task.cpp`, `flash.cpp`, `ota_health.h`, `github.cpp`, `nvs_utils.h`, `nvs_keys.h`, `app_config.cpp`, `system_lifecycle.cpp`, `main.cpp`, `task_watchdog.h`, `tls_bundle_setup.cpp`.

**Interleavings:**

- `otaBlocksDestructiveAction` auf Reset, WiFi-Apply, Flash, Reboot, Soft-off
- OTA-Task: TWDT unsubscribe während Block, danach wieder subscribe; Stack 12288
- `verifyRollbackLater` / 30 s stabile STA bevor mark-valid
- `g_nvsMutex` deckt alle Preferences (`wifi`/`mqtt`/`cfg`/`chaya`)? Direkte `Preferences` ohne Wrapper?
- Counter-Debounce ≥30 s: `flushAllHeartCountersIfDirty` auf Soft-off, Factory-Reset, OTA-Reboot
- packed blobs `cfg_v2` / `baseBlob`, Migration `cred_v1`, Partial Write
- Factory-Reset: RAM-Reset vs. NVS-Delete vs. Restart-Reihenfolge; andere Tasks lesen alten State
- Init-Order in `main.cpp`: Queues/Mutexes vor Tasks; SoftAP-QR vor RF; `buttonStartupBlink` vor Button-Task
- TLS-Bundle-Mutex: MQTT + OTA gleichzeitig init?
- `millis()`-Wrap in OTA/Health/Lifecycle

MQTT-Config-Inhalt → Agent 1; WiFi-NVS-Pack-Format → Agent 2 (hier nur Mutex/Flush/Reset-Order).

---

## Agent 6 — Frontend / Flasher / Vertrag

**Scope:** `frontend/`, `flasher/`, `docs/openapi.yaml`, `docs/asyncapi.yaml`, `frontend/mock/`.

**Lesen:** `frontend/src/state/device.svelte.ts`, `frontend/src/api/sse.ts`, `client.ts`, `ota.ts`, `validate.ts`, `types.ts`, `mock/deviceState.ts`, `mock/mockPlugin.ts`, `flasher/src/flash/flashFirmware.ts`, `flashVerify.ts`, `webSerial.ts`, MQTT/WiFi/OTA-Pages soweit sie State schreiben.

**Interleavings:**

- SSE vs. Fetch überschreibt Formular-Wahrheit **trotz** vorhandener Generation-Gates
- STA-WLAN-Save: leeres Passwort wischt NVS-PSK und rebootet ohne SoftAP — nur wenn der Pfad **jetzt** so sendet/speichert
- Flasher: paralleles Flash ohne Job-Lock
- Echter Vertragsbruch OpenAPI ↔ Route ↔ `types.ts` (nicht Mock)

Keine Firmware-Internals außer zum Abgleich der Invarianten.

---

## Merge-Block (nach den sechs Agents)

Du führst sechs Reports zusammen. Verifiziere jedes Finding kurz am **aktuellen** Code (kein neuer Full-Audit).

1. Code schlägt Report. Fix schon da → streichen, nicht „fast noch“.
2. Duplikate an Scope-Grenzen: eine ID.
3. Streichen: Hardening, Latent race, HIL-only, Zwei-Tab, NVS-voll, ms-Fenster, dokumentiertes Design.
4. High nur nach der High-Regel. Keine drei „kritischsten Punkte“, wenn weniger übrig sind.
5. Schreibe `audit/FINDINGS.md` schlank: Kurzfassung, nur überlebende Findings, Hotspots ohne Befund, HIL-Fragen, gedroppte IDs.

Leere Datei mit „Keine Findings“ ist ein gültiges Ergebnis. Keine 15er-Liste.
