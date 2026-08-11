# Testing

Lokale Testpyramide für chaya2mqtt nach dem **Prinzip**: echte Firmwarelogik läuft nativ mit Host-Fakes; die React-GUI wird mit Vitest und Playwright gegen den Geräte-Mock geprüft. Keine automatisierten On-Device-/HIL-Tests.

## Testpyramide

```text
                 ┌────────────────────────────┐
                 │ Manuelle Hardware-Abnahme  │  ESP32 + Broker + Display
                 │ scripts/simulator.py       │
                 └─────────────┬──────────────┘
                               │
           ┌───────────────────┴───────────────────┐
           │ Browser-E2E (Playwright + Geräte-Mock)│
           │ npm run test:e2e / make test-e2e      │
           └───────────────────┬───────────────────┘
                               │
     ┌─────────────────────────┴─────────────────────────┐
     │ Frontend Vitest + Coverage                        │
     │ Native Unity + Device-Simulator + ASan/UBSan      │
     │ cppcheck + Python Embed-Tests                     │
     └───────────────────────────────────────────────────┘
```

## Modul → Test-Matrix

| Modul | Produktionspfade | Native / Sim | Frontend |
|-------|------------------|--------------|----------|
| MQTT Pairing / Topics / Port | `src/mqtt/pairing.h`, `mqtt_config.*` | `test/test_mqtt` | `MqttPage`, contract |
| MQTT Backoff / Reconnect | `src/mqtt/backoff.h`, `mqtt_reconnect.cpp` | `test_mqtt`, `test_device_sim` | — |
| Counter Payload / Display-Delta | `counter_payload.h`, `heart/counter_pure.h` | `test_mqtt`, `test_time`, device-sim | Dashboard SSE |
| WiFi / NVS-Pack | `wifi/wlan_pack.h`, `wlan_config.*` | `test_wifi`, device-sim NVS | `WifiSetup`, E2E AP |
| Web CSRF / Host / Hex / SPA | `csrf_pure.h`, `hex_codec.h`, `host_validate.h`, `spa_asset_lookup.h` | `test_web` | `client` CSRF, contract |
| OTA Version / GitHub JSON | `ota/version_cmp.h`, `github_parse.h` | `test_ota` | `UpdatePage`, E2E |
| OTA Health-Fenster (30 s) | `ota/ota_health.h` | `test_ota` (`test_ota_health_window`) | — |
| WLAN Forced-Reassoc-Schwelle | `wifi/wlan_config.h` | `test_wifi` | — |
| Zeithelfer | `util/time_helpers.h` | `test_time` | — |
| Device-Orchestrierung | reine Helper + `sim/device_runtime.h` | `test_device_sim` | Mock-Szenarien |
| REST/SSE-Vertrag | `admin_routes_api.cpp`, `web_events.*` | — | `contract.test.ts`, OpenAPI/AsyncAPI |
| SPA-UI | `frontend/src/**` | — | Vitest Pages/Components, Playwright |

Generierte Artefakte (`frontend/coverage/`, `frontend/test-results/`, `playwright-report/`) sind gitignored und werden nicht versioniert.

## Voraussetzungen

- Node.js + npm (Frontend)
- PlatformIO CLI (Makefile default: `~/.platformio/penv/bin/pio`)
- Python 3 (Embed-Tests, optional `paho-mqtt` für Hardware-Smoke)
- Playwright-Browser einmalig: `cd frontend && npx playwright install chromium`
- Für ASan: Host-Clang/GCC mit AddressSanitizer/UBSan (macOS Xcode CLT reicht)

## Schnelle Kommandos

| Target / Befehl | Inhalt |
|-----------------|--------|
| `make check-quick` | Lint, Format, Vitest, native Unity |
| `make check-pr` | quick + ASan/UBSan + cppcheck |
| `make check` | Vollständiger hardwarefreier Gate vor Commits |
| `make check-static` | cppcheck über `src/` (`tool-cppcheck`, Arduino-False-Positives unterdrückt) |
| `make test-frontend` | Vitest |
| `make test-coverage` | Vitest mit Coverage-Schwellen |
| `make test-native` | `pio test -e native` (inkl. Device-Simulator) |
| `make test-asan` | `pio test -e native-asan` |
| `make device-sim` | Nur Device-Simulator (`pio test -e device-sim`) |
| `make test-e2e` | Playwright gegen Vite-Mock |
| `make test-e2e-smoke` | Playwright `@smoke` |
| `python3 scripts/simulator.py --smoke` | MQTT-Smoke gegen echten Broker/ESP |

## Gates

### `make check-pr` (schnell)

1. Frontend: `npm ci`, lint, format:check, Vitest  
2. Native Unity (`native`)  
3. Native ASan/UBSan (`native-asan`)  
4. cppcheck (`esp32dev`, fail on high)

### `make check` (vollständig)

Alles aus `check-pr` plus Coverage-Schwellen, Frontend-Build, SPA-Embed + Python-Embed-Tests, Playwright E2E, ESP32-Release-Build (`CHAYA_SKIP_FRONTEND_BUILD=1` wenn `web_ui.bin` schon aus dem Frontend-Build stammt).

Der kostenintensive Check-Workflow ist im privaten Repository bewusst deaktiviert; alle Qualitätsgates laufen lokal. Nur der seltene Tag-basierte Release-Workflow bleibt aktiv.

## Frontend

- **Unit/Integration:** Vitest + Testing Library (`frontend/src/**/*.test.*`, `frontend/mock/**/*.test.ts`)
- **Coverage:** `npm run test:coverage` — Schwellen in `frontend/vite.config.ts` (70 % Lines/Functions/Statements, 60 % Branches)
- **Vertrag:** `frontend/src/api/contract.test.ts` hält Mock, Firmware-Routen, OpenAPI/AsyncAPI und MQTT-Felder synchron
- **E2E:** `frontend/e2e/` mit Szenario-Reset über `/api/_mock/scenario`

## Native C++ / Device-Simulator

Header-only / reine Logik unter `src/` wird mit Unity getestet. Hardware-APIs werden nicht nachgebaut — nur Ports über `sim/`:

| Fake | Zweck |
|------|--------|
| `sim/fake_clock.h` | Deterministische Zeit |
| `sim/fake_nvs.h` | WiFi/MQTT-Persistenz + Fehler-Injektion |
| `sim/fake_network.h` | WiFi/NTP-Status |
| `sim/fake_mqtt_transport.h` | Connect/Publish/Subscribe-Log |
| `sim/device_runtime.h` | Orchestrierung mit Produktions-Helpers |

Szenarien in `test/test_device_sim/`: First-Connect/Pair, Disconnect/Reconnect, Broker-Sanitizing, Unpair/Publish, Publish-Fehler, NVS-Restart, WiFi-down, Connect-Fail-Backoff, NTP-Defer, Counter-Pfad, Broker-Ausfall bei stabilem WLAN.

Native Pure-Helpers zusätzlich: WLAN-Recovery-Entscheidungen und Captive-Portal-Pfad-Erkennung.

## Hardware-Abnahme (manuell)

Benötigt geflashten ESP32, WLAN und erreichbaren TLS-Broker. Die lokale MQTT-Konfiguration steht am Anfang von `scripts/simulator.py`; echte Zugangsdaten dürfen nicht committed werden.

```bash
python3 scripts/simulator.py --smoke
```

Alle Werte können alternativ über `--host`, `--port`, `--user`, `--pass`, `--topic-sub` und `--topic-pub` übergeben werden.

### Checkliste

1. **Flash/Boot** — Release flashen, Serial zeigt Boot ohne Panic; Display aktualisiert.
2. **AP-Setup** — offener SoftAP `Chaya2MQTT`; Captive Portal (`/generate_204` etc.); SSID-Scan; Test & Connect; Commit.
3. **WLAN-Wechsel/Recovery** — falsches Passwort → Fehler; korrekt → STA; Reboot behält Config; wiederholte Disconnects → Soft-Reconnect dann Forced-Reassoc; längerer Ausfall → kontrollierter Restart (kein Restart während OTA); LOST_IP löst denselben Reconnect-Pfad aus.
4. **MQTT Pairing/Telemetrie** — Broker + Partner; LWT online; Heart senden/empfangen; während MQTT-down Modem-PS aus (`WIFI_PS_NONE`), nach Connect wieder `MIN_MODEM`.
5. **Broker-Ausfall** — Broker neu starten bei stabilem WLAN → MQTT-Backoff/Reconnect ohne Factory Reset; LWT offline→online.
6. **WLAN-Unterbrechung** — Access Point kurz ausschalten → STA-Reconnect → MQTT wieder online.
7. **Display** — RX/TX-Änderung sichtbar; AP-Splash zeigt SSID und Setup-URL/IP.
8. **Neustart/Persistenz** — Power-Cycle behält WLAN, MQTT und Counter.
9. **OTA** — Check; Fehlerpfad ohne Netz zeigt Error-Phase; während Download kein Factory Reset; nach OTA-Boot **≥30 s** settled ohne Panic → kein Rollback (MQTT nicht nötig).
10. **USB-Recovery** — bei Bedarf `make upload-release-clean`; Core-Dump optional via `make analyze-coredump`.
11. **PCB-Gate** — `scripts/pcb_erc_drc.sh`; BOM-`VERIFY`-Zeilen (EPD-HV) vor Bestellung manuell freigeben.

## Definition of Done

- Bugfix → Regressionstest (native oder Vitest/E2E)
- Neue Geschäftslogik → host-testbarer Pure-Header/Helper + Unity-Case
- REST/SSE-Änderung → `contract.test.ts` + OpenAPI/AsyncAPI + Mock aktualisieren
- Hardware-Adapter → mindestens erfolgreicher `esp32dev-release`-Build
- Vor Commit: `make check` (Cursor-Regel)

## Grenzen

- Keine automatisierten On-Device-/HIL-Tests
- Kein vollständiger Arduino-Host-Shim (fokussierter Simulator, analog Idee)
- TLS-/Timing-/Funk-Eigenschaften nur auf Hardware prüfbar
- Playwright braucht einmalig installierte Browser-Binaries
