# Testing

Local test pyramid for chaya2mqtt based on this **principle**: real firmware logic runs natively with host fakes; the Svelte GUI is tested with Vitest and Playwright against the device mock. There are no automated on-device/HIL tests.

## Test pyramid

```text
                 ┌────────────────────────────┐
                 │ Manual hardware acceptance │  ESP32-S3 1.54G + broker + display
                 │ scripts/simulator.py       │
                 └─────────────┬──────────────┘
                               │
           ┌───────────────────┴───────────────────┐
           │ Browser E2E (Playwright + device mock)│
           │ npm run test:e2e                       │
           └───────────────────┬───────────────────┘
                               │
     ┌─────────────────────────┴─────────────────────────┐
     │ Frontend Vitest + Coverage                        │
     │ Native Unity + device simulator + ASan/UBSan      │
     │ cppcheck + Python embedding tests                 │
     └───────────────────────────────────────────────────┘
```

## Module → test matrix

| Module | Production paths | Native / simulator | Frontend |
|--------|------------------|--------------------|----------|
| MQTT Pairing / Topics / Port | `src/mqtt/pairing.h`, `mqtt_config.*` | `test/test_mqtt` | `MqttPage`, contract |
| MQTT Backoff / Reconnect | `src/mqtt/backoff.h`, `mqtt_reconnect.cpp` | `test_mqtt`, `test_device_sim` | — |
| Counter Payload / Display-Delta | `counter_payload.h`, `heart/counter_pure.h` | `test_mqtt`, `test_time`, device-sim | Dashboard SSE |
| WiFi / NVS-Pack | `wifi/wlan_pack.h`, `wlan_config.*` | `test_wifi`, device-sim NVS | `WifiSetup`, E2E AP |
| Web CSRF / Host / Hex / SPA | `csrf_pure.h`, `hex_codec.h`, `host_validate.h`, `spa_asset_lookup.h` | `test_web` | `client` CSRF, contract |
| OTA Version / GitHub JSON | `ota/version_cmp.h`, `github_parse.h` | `test_ota` | `UpdatePage`, E2E |
| OTA health window (30 s) | `ota/ota_health.h` | `test_ota` (`test_ota_health_window`) | — |
| WiFi forced-reassociation threshold | `wifi/wlan_config.h` | `test_wifi` | — |
| Time helpers | `util/time_helpers.h` | `test_time` | — |
| Battery / audio gates / yellow flags | `hw/battery_pure.h`, `audio/audio_pure.h`, `display/draw_pure.h` | `test_hw` | Settings, dashboard |
| Device orchestration | Pure helpers + `sim/device_runtime.h` | `test_device_sim` | Mock scenarios |
| REST/SSE contract | `admin_routes_api.cpp`, `web_events.*` | — | `contract.test.ts`, OpenAPI/AsyncAPI |
| SPA-UI | `frontend/src/**` | — | Vitest Pages/Components, Playwright |

Generated artifacts (`frontend/coverage/`, `frontend/test-results/`, `playwright-report/`) are gitignored and not versioned.

## Prerequisites

- Node.js + npm (frontend)
- PlatformIO CLI (Makefile default: `~/.platformio/penv/bin/pio`)
- Python 3 (embedding tests, optional `paho-mqtt` for the hardware smoke test)
- Install the Playwright browser once: `cd frontend && npx playwright install chromium`
- For ASan: host Clang/GCC with AddressSanitizer/UBSan (macOS Xcode CLT is sufficient)

## Quick commands

| Target / command | Purpose |
|------------------|---------|
| `make check` | Complete hardware-free gate before commits |
| `cd frontend && npm test` | Vitest |
| `cd frontend && npm run test:coverage` | Vitest with coverage thresholds |
| `pio test -e native` | Native Unity including the device simulator |
| `pio test -e native-asan` | Native Unity with ASan/UBSan |
| `cd frontend && npm run test:e2e` | Playwright against the Vite mock |
| `python3 scripts/simulator.py --smoke` | MQTT smoke test against a real broker/ESP |

## Gates

### `make check` (complete)

Frontend linting and formatting, coverage thresholds, frontend build, SPA embedding + Python embedding tests, native Unity, ASan/UBSan, cppcheck, Playwright E2E, and the ESP32-S3 release build.

The expensive check workflow is intentionally disabled in the private repository; all quality gates run locally. Only the infrequent tag-based release workflow remains active.

## Frontend

- **Unit/integration:** Vitest + Testing Library (`frontend/src/**/*.test.ts`, `frontend/mock/**/*.test.ts`)
- **Coverage:** `npm run test:coverage`—thresholds in `frontend/vite.config.ts` (70% lines/functions/statements, 60% branches)
- **Contract:** `frontend/src/api/contract.test.ts` keeps the mock, firmware routes, OpenAPI/AsyncAPI, and MQTT fields synchronized
- **E2E:** `frontend/e2e/` with scenario reset through `/api/_mock/scenario`

## Native C++ / device simulator

Header-only / pure logic under `src/` is tested with Unity. Hardware APIs are not recreated—only ports through `sim/`:

| Fake | Purpose |
|------|---------|
| `sim/fake_clock.h` | Deterministic time |
| `sim/fake_nvs.h` | WiFi/MQTT persistence + fault injection |
| `sim/fake_network.h` | WiFi/NTP status |
| `sim/fake_mqtt_transport.h` | Connect/publish/subscribe log |
| `sim/device_runtime.h` | Orchestration with production helpers |

Scenarios in `test/test_device_sim/`: first connect/pair, disconnect/reconnect, broker sanitization, unpair/publish, publish error, NVS restart, WiFi down, connection-failure backoff, NTP deferral, counter path, broker failure with stable WiFi.

Additional native pure helpers: WiFi recovery decisions and captive portal path detection.

## Hardware acceptance (manual)

Requires a flashed ESP32-S3-ePaper-1.54G, WiFi, and a reachable TLS broker. The local MQTT configuration is at the beginning of `scripts/simulator.py`; real credentials must not be committed.

```bash
python3 scripts/simulator.py --smoke
```

Alternatively, all values can be passed through `--host`, `--port`, `--user`, `--pass`, `--topic-sub`, and `--topic-pub`.

### Checklist

1. **Flash/boot**—flash the release over USB-C; on battery press PWR and confirm GPIO17 stays latched; display updates (~20 s). Do not hold BOOT except for download.
2. **AP setup**—open `Chaya2MQTT` SoftAP; captive portal (`/generate_204`, etc.); SSID scan; test & connect; commit.
3. **WiFi change/recovery**—wrong password → error; correct password → STA; reboot retains configuration; repeated disconnects → soft reconnect, then forced reassociation; longer outage → controlled restart (no restart during OTA); LOST_IP triggers the same reconnect path.
4. **MQTT pairing/telemetry**—broker + partner; LWT online; send/receive heart; while MQTT is down, modem power saving is off (`WIFI_PS_NONE`), and after connection it returns to `MIN_MODEM`.
5. **Broker outage**—restart broker with stable WiFi → MQTT backoff/reconnect without factory reset; LWT offline → online.
6. **WiFi interruption**—briefly turn off the access point → STA reconnect → MQTT online again.
7. **Display**—RX/TX change visible on the 1.54G (red heart); AP splash shows SSID and setup URL/IP; refresh ~20 s.
8. **Restart/persistence**—USB or battery power cycle retains WiFi, MQTT, and counters; PWR+GPIO17 latch still holds on LiPo.
9. **OTA**—check; error path without network shows error phase; no factory reset during download; after OTA boot, **≥30 s** settled without panic → no rollback (MQTT not required).
10. **USB recovery**—erase flash with PlatformIO if needed and reflash the release; optionally analyze a core dump with `scripts/analyze_coredump.py`.

## Definition of Done

- Bug fix → regression test (native or Vitest/E2E)
- New business logic → host-testable pure header/helper + Unity case
- REST/SSE change → update `contract.test.ts` + OpenAPI/AsyncAPI + mock
- Hardware adapter → at least a successful `esp32s3-release` build
- Before committing: `make check` (Cursor rule)

## Limitations

- No automated on-device/HIL tests
- No complete Arduino host shim (focused simulator using an analogous approach)
- TLS/timing/radio properties can only be tested on hardware
- Playwright requires browser binaries to be installed once
