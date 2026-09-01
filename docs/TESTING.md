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
     │ cppcheck + clang-tidy (*_pure.h) + embed tests    │
     └───────────────────────────────────────────────────┘
```

## Module → test matrix

| Module | Production paths | Native / simulator | Frontend |
|--------|------------------|--------------------|----------|
| MQTT Pairing / Topics / Port | `src/mqtt/pairing.h`, `mqtt_config.*` | `test/test_mqtt` | `MqttPage`, contract |
| MQTT QoS-1 ACK / Backoff / Reconnect | `mqtt_publish_ack.h`, `backoff.h`, `mqtt_reconnect.cpp` | `test_mqtt`, `test_device_sim` | — |
| Counter Payload / Display-Delta | `counter_payload.h`, `heart/counter_pure.h` | `test_mqtt`, `test_time`, device-sim | Dashboard SSE |
| WiFi / NVS-Pack | `wifi/wlan_pack.h`, `wlan_config.*` | `test_wifi`, device-sim NVS | `WifiSetup`, E2E AP |
| Web CSRF TTL/Grace / Host / Hex / SPA | `csrf_pure.h`, `hex_codec.h`, `host_validate.h`, `spa_asset_lookup.h` | `test_web` | single-flight refresh/retry, contract |
| OTA Version / GitHub JSON / URL allowlist | `version_cmp.h`, `github_parse.h`, `ota_url_allow.h` | `test_ota` | `UpdatePage`, E2E |
| OTA health window (30 s) | `ota/ota_health.h` | `test_ota` (`test_ota_health_window`) | — |
| WiFi Soft→Force reconnect threshold | `wifi/wlan_config.h` (`kWifiSoftReconnectAttemptsBeforeForce`) | `test_wifi` (constant assert only) | Soft-reconnect recovery loop: HIL/manual |
| Time helpers | `util/time_helpers.h` | `test_time` | — |
| Battery / audio gates / queue coalescing / display link + battery icons / LED patterns / button debounce | pure helpers in `hw`, `audio`, `async`, `display`, `button`, `led` | `test_hw` | Settings, dashboard |
| Device orchestration | Pure helpers + `sim/device_runtime.h` | `test_device_sim` | Mock scenarios |
| REST/SSE contract | `admin_routes_api*.cpp`, `events.*` | — | `contract.test.ts`, OpenAPI/AsyncAPI |
| SPA-UI | `frontend/src/**` | — | Vitest Pages/Components, Playwright |

Generated artifacts (`frontend/coverage/`, `frontend/test-results/`, `playwright-report/`) are gitignored and not versioned.

## Prerequisites

- Node.js + npm (frontend)
- PlatformIO CLI (Makefile default: `~/.platformio/penv/bin/pio`)
- Python 3 (embedding tests, optional `paho-mqtt` for the hardware smoke test)
- Install the Playwright browser once: `cd frontend && npx playwright install chromium`
- For ASan: host Clang/GCC with AddressSanitizer/UBSan (macOS Xcode CLT is sufficient)
- clang-tidy: required in CI (`scripts/check_pure_clang_tidy.sh`). Locally optional; without it `make check-firmware-tests` skips TEST-05. Homebrew: `llvm` on `PATH`.

## Quick commands

| Target / command | Purpose |
|------------------|---------|
| `make check` | Complete hardware-free gate before commits |
| `make check-frontend` | Frontend lint, svelte-check, coverage, build, and Playwright E2E |
| `make check-flasher` | Web-flasher lint, type-check, build, and Python tests |
| `make check-firmware` | Native/ASan tests, cppcheck, and release build |
| `cd frontend && npm test` | Vitest |
| `cd frontend && npm run test:coverage` | Vitest with coverage thresholds |
| `pio test -e native` | Native Unity including the device simulator |
| `pio test -e native-asan` | Native Unity with ASan/UBSan |
| `cd frontend && npm run test:e2e` | Playwright against the Vite mock |
| `python3 scripts/simulator.py --smoke` | Automated broker/ESP reachability plus simulator QoS-1 PUBACK |
| `python3 scripts/simulator.py --hardware-smoke` | Manual SKU 34586 counter/PUBACK acceptance |
| `python3 scripts/test_flasher_site.py` | Web-flasher site / release artifact unit tests |
| `make flasher RELEASES_DIR=...` | Generate `flasher/_site` for local preview |

## Gates

### `make check` (complete)

Frontend linting and formatting, coverage thresholds, frontend build, SPA embedding + Python embedding tests, flasher/release artifact tests, native Unity, ASan/UBSan, cppcheck, clang-tidy on `*_pure.h`, Playwright E2E, the ESP32-S3 release build, and `prepare_release_artifacts.py` validation.

For pull requests, the `Quality gate` workflow selects the affected frontend, flasher, and firmware
jobs from the changed paths and runs them in parallel. Documentation-only changes finish without
starting build jobs. Pushes to `main` still run the complete gate in parallel.
The tag-based release workflow also runs the complete gate before publishing artifacts.
The Pages deploy workflow publishes the browser flasher to
[`eyjunge1.github.io/chaya2mqtt`](https://eyjunge1.github.io/chaya2mqtt/).

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
| `sim/fake_mqtt_transport.h` | Connect/publish/subscribe log with message IDs |
| `sim/device_runtime.h` | Orchestration with ACK, pending-send, and disconnect state |

Scenarios in `test/test_device_sim/` additionally cover pending PUBACK, competing sends, exact-once
counter application, and disconnect cancellation. These deterministic tests cover critical
interleavings without claiming to emulate the ESP-IDF scheduler or radio hardware.

Additional native pure helpers: WiFi recovery decisions and captive portal path detection.

## Hardware acceptance (manual)

Requires a flashed ESP32-S3-ePaper-1.54G, WiFi, and a reachable TLS broker. The local MQTT configuration is at the beginning of `scripts/simulator.py`; real credentials must not be committed.

```bash
python3 scripts/simulator.py --hardware-smoke
```

The command verifies the simulator's own QoS-1 PUBACK, waits for an initial retained ESP counter,
then asks for one button/Web-Admin heart and requires the device counter to advance. Alternatively,
all values can be passed through `--host`, `--port`, `--user`, `--pass`, `--topic-sub`, and
`--topic-pub`. This remains a local manual test on SKU 34586; GitHub Actions has no permanently
attached device and `make check` stays hardware-free.

### Checklist

1. **Flash/boot**—flash the release over USB-C; on battery press PWR and confirm GPIO17 stays latched; display updates (~20 s). Do not hold BOOT except for download.
2. **AP setup**—scan the WIFI QR on the display (phone camera); join WPA2/WPA3 `Chaya2MQTT`; confirm `chaya2mqtt.local` and captive probes reach the connected AP; on **iPhone**, confirm the Captive Network Assistant (or Safari) loads the Wi‑Fi setup SPA with gzip `/assets/*` (page renders, not a download); SSID scan; test & connect; commit.
3. **Multi-device network identity**—start two unconfigured devices; scan each display's WIFI QR to configure the two same-named but isolated setup APs. After both join the same LAN, confirm `chaya2mqtt-<id-a>.local` and `chaya2mqtt-<id-b>.local` consistently open the matching dashboards and expose matching IDs. Confirm the unsuffixed STA name does not select an arbitrary device.
4. **WiFi change/recovery**—wrong password → error; correct password → STA; reboot retains configuration; repeated disconnects → soft reconnect, then forced reassociation (Soft→Force threshold is unit-asserted only; full soft-reconnect recovery loop is HIL/manual); longer outage → controlled restart (no restart during OTA); LOST_IP triggers the same reconnect path.
5. **MQTT pairing/telemetry**—broker + partner; LWT online; send/receive heart; while MQTT is down, modem power saving is off (`WIFI_PS_NONE`), and after connection it returns to `MIN_MODEM`. Unpair → waiting `Chaya2MQTT` title stays; web send disabled and device button does not send until partner is set again.
6. **Broker outage**—restart broker with stable WiFi → MQTT backoff/reconnect without factory reset; LWT offline → online.
7. **WiFi interruption**—briefly turn off the access point → STA reconnect → MQTT online again.
8. **Display**—RX/TX change visible on the 1.54G (red heart); AP splash shows the WIFI QR (phone camera join); refresh ~20 s.
9. **Restart/persistence**—USB or battery power cycle retains WiFi, MQTT, and counters; PWR+GPIO17 latch still holds on LiPo.
10. **OTA**—check; error path without network shows error phase; no factory reset during download; after OTA boot, **≥30 s** settled without panic → no rollback (MQTT not required).
11. **USB recovery**—prefer the web flasher or PlatformIO erase + reflash; optionally analyze a core dump with `scripts/analyze_coredump.py`.
12. **Web flasher (manual)**—serve `flasher/_site` on localhost; Chrome/Edge or another Chromium browser; Stable and Beta channel switch; BOOT only if the port is missing; after flash, SoftAP `Chaya2MQTT` and captive setup.

## Definition of Done

- Bug fix → regression test (native or Vitest/E2E)
- New business logic → host-testable pure header/helper + Unity case
- REST/SSE change → update `contract.test.ts` + OpenAPI/AsyncAPI + mock
- Hardware adapter → at least a successful `esp32s3-release` build
- Before committing: `make check`

## Limitations

- No automated on-device/HIL tests
- No complete Arduino host shim (focused simulator using an analogous approach)
- TLS/timing/radio properties can only be tested on hardware
- Playwright requires browser binaries to be installed once
