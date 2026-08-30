# Code Reference (Modules)

Overview of all source modules under `src/`, with correct paths, responsibilities, and important APIs.

## Theme map (`src/`)

**Folder = theme.** Pick the domain folder first; use ownership below when unsure.

```text
src/
  main.cpp                 Bootstrap: init order, start tasks
  constants.h              Cross-cutting device constants

  identity/                Stable device ID, hostname (NVS cfg/device_id)
  button/                  BOOT/PWR, debounce, send gesture (calls led/)
  led/                     Blink/presets, refresh pulse, GPIO LED
  battery/                 ADC, percent, power-off
  hw/                      Power-save hold-off (sd_hold) + pin map (pins*)

  display/                 E-Ink, draw, QR, display task
  audio/                   ES8311 playback, audio task

  wifi/                    STA/AP, captive DNS, scan, recovery, NVS
  mqtt/                    Client, pub/sub, reconnect, broker config (mqtt/config.*)
  network/                 One task: wlanLoop + mqttLoop + NetCmd
  tls/                     Embedded CA bundle (MQTT + OTA)
  ota/                     Update check, download, flash, health

  heart/                   RX/TX counters, baselines, NVS chaya
  config/                  App prefs, NVS utils/keys, version (not MQTT broker)

  web/                     Admin site: server, CSRF, middleware, SPA, assets
    routes/                HTTP routes (JSON /api/*, captive, SPA)
    events.*               SSE /events

  async/                   FreeRTOS queues, mutexes, event types, app task
  diag/                    Watchdog, stack monitor
  util/                    Log macro, time, IP, small helpers
```

### Ownership

| Put it in | When |
|-----------|------|
| Domain folder | Domain state + persistence; NVS writes via `app_nvs` + keys in `config/nvs_keys.h` |
| `config/` | NVS infra (`nvs_utils`, `nvs_keys`) + app prefs (`cfg`) — not MQTT/WiFi/heart payloads |
| `web/` | HTTP adapter only (calls domain APIs) |
| `async/` | FreeRTOS queues/handles (not HTTP) |
| `button/` / `led/` / `battery/` | Input / light / power |
| `hw/` | Disable peripherals (hold-off) + board pin map |
| `util/` | Helpers with no domain |

### NVS model

One infra, many domain writers:

| Layer | Where | Role |
|-------|--------|------|
| NVS infra | `config/nvs_utils.h`, `config/nvs_keys.h` | Mutex/helpers + all namespace/key names |
| Domain writers | `mqtt/config.cpp`, `wifi/wlan_nvs.cpp`, `heart/counter_nvs.cpp`, `config/app_config.cpp`, `identity/`, … | Domain logic; only `app_nvs::…(kNvsNs…, kNvsKey…)` |

Namespaces stay separate (`wifi` / `mqtt` / `cfg` / `chaya`). Do not fold MQTT/WiFi/heart persistence into `app_config`.

**Notes:** Pins live in `hw/` (shared board map). `button/` and `led/` are separate folders; other packages may call `led/`. `mqtt/config.*` is broker config only; `config/` is app prefs + NVS infra.

---

## `main.cpp`

**Purpose:** Bootstrap and task startup. No traditional Arduino `loop()` style.

**Setup sequence:**
1. `BAT_Control` (GPIO17) HIGH, then `sdHoldOff()` (microSD lines LOW)
2. `asyncInfraInit()`—queues + mutexes
3. CPU 240 MHz, BT off, DFS (no light sleep)
4. `displayInit()` + `displayStartTask()`
5. `buttonInit()`, `batteryInit()`, `audioInit()` (mic/capture off)
6. Serial 115200 only when `CORE_DEBUG_LEVEL > 0`
7. Load NVS: MQTT, counters, reset period, UI prefs, LED, audio, display view
8. If no STA credentials: arm setup AP and paint SoftAP WIFI QR **before** Wi‑Fi RF
9. `setupWiFi()`—STA or AP (operational heart/splash is queued after STA `GOT_IP` or SoftAP ready)
10. `mqttSetup()`
11. `buttonStartupBlink()` (before the button task!)
12. `audioStartTask()`, `buttonStartTask()`, `networkTaskStart()`, `otaTaskStart()`, `appTaskStart()`
13. `ledEnableGpioHoldForLightSleep()`

**`loop()`:** `vTaskDelete(nullptr)`—terminates immediately.

---

## `async/` – infrastructure

### `async/event_types.h`

```cpp
enum class NetCmd : uint8_t {
    MqttSettingsChanged, MqttKillClient, WifiGotIp, WifiReconnect,
    ChayaSendRequested, FactoryResetRequested,
};

struct DisplayMsg {
    enum class Cmd : uint8_t { DrawHeart, DrawSplash, DrawPowerOff };
    Cmd cmd;
    uint32_t payload;
};

struct AudioMsg {
    enum class Kind : uint8_t { Tx, Rx };
    Kind kind;
};
```

### `async/task_handles.h` / `task_handles.cpp`

Global queues and mutexes. `asyncInfraInit()` creates:
- `g_netCmdQueue` (32 × `NetCmd`)
- `g_displayCmdQueue` (32 × `DisplayMsg`)
- `g_audioCmdQueue` (4 × `AudioMsg`)
- 6 mutexes and the button-completion binary semaphore (see [ARCHITECTURE.md](ARCHITECTURE.md))

`netCmdTrySend(NetCmd, waitTicks=0)` is the single enqueue helper for the network-task command queue (Wi‑Fi events, MQTT kill, factory reset, MQTT settings apply).

The TLS bundle and MQTT configuration modules own additional internal mutexes.

### Preset APIs (domain entry points)

Prefer a small enum + one function over parallel wrappers that do “the same thing a bit differently”:

| Domain | Preset / cmd | Entry |
|--------|--------------|--------|
| LED | `LedPreset` (Boot, WifiUp, MqttUp, LinkDown, SoftOff) | `ledPlayPreset` / Blocking |
| Display | `DisplayMsg::Cmd` + `DisplayRequestMode` | `displayRequest(cmd, mode, waitMs)` |
| Audio | `AudioMsg::Kind` (Tx, Rx) | `audioRequest(kind)` |
| Chaya send | — | `chayaRequestSend()` (button + web; same LED TX sequence) |
| NetCmd | `NetCmd` | `netCmdTrySend(cmd)` |

### `async/app_task.cpp`

App task (4096 stack, priority 4, core 1), loop every 500 ms:
- OTA health: after 30 s since WiFi boot settlement → `otaTryMarkValidAfterHealthCheck()`
- `webAdminLoop()`—deferred web work, SSE
- `maybePeriodicallyResetCounters()`—STA mode only
- `maybeResetDisplayBaselinesWhenCapped()`—STA mode only
- `maybeSaveAllHeartCounters()`
- Battery ADC sample (~every 30 s)
- Periodic heap logging (free/min/largest)

---

## `heart/counter` – counter logic

**Files:** `heart/counter.h`, `heart/counter_internal.h`, `heart/counter.cpp`, `heart/counter_nvs.cpp`, `heart/counter_sync.cpp`

| File | Responsibility |
|------|----------------|
| `counter.cpp` | Atomics, display deltas, factory RAM reset |
| `counter_nvs.cpp` | NVS loading/saving, debounced saves (≥30 s) |
| `counter_sync.cpp` | Periodic baseline roll, cap reset at ≥999 |

### Global variables

| Symbol | Type | Description |
|--------|------|-------------|
| `heartCounter` | `std::atomic<int>` | Received value (MQTT subscribe) |
| `heartSentCounter` | `std::atomic<int>` | Successfully sent values |
| `counterBaseline` | `std::atomic<int>` | RX display baseline |
| `sentCountBaseline` | `std::atomic<int>` | TX display baseline |

### Important functions

| Function | Description |
|----------|-------------|
| `heartDisplayRxDelta()` / `heartDisplayTxDelta()` | Delta = raw − baseline, capped |
| `heartCounterStoreFromRemote(int)` | Set received value (thread-safe) |
| `heartSentCounterApplyAfterSuccessfulPublish()` | Increment TX counter |
| `heartCounterFillDrawSnapshot(...)` | Atomic snapshot for the display |
| `loadHeartCounter()` | Read NVS `chaya` |
| `persistCounterBaselineState()` | Write packed `baseBlob` (baselines + reset day) |
| `maybeSaveAllHeartCounters()` | Debounced save for RX + TX (≥30 s) |
| `maybeSaveHeartSentCounter()` | Debounced save for TX only (publish ack) |
| `flushAllHeartCountersIfDirty()` | Flush RX + TX if dirty |
| `maybePeriodicallyResetCounters()` | Periodic baseline roll (UTC days) |
| `maybeResetDisplayBaselinesWhenCapped()` | Baseline roll when display reaches ≥999 |
| `counterResetRamAfterFactoryClear()` | Reset RAM after factory reset |

NVS debouncing: saves only every **≥30 s** (`kHeartCounterSaveMinIntervalMs`).

---

## `identity/` – stable device and network identity

**Files:** `identity/device_identity.h`, `identity/device_identity.cpp`, `identity/device_identity_pure.h`

`buildDeviceId()` loads or creates the six-character ID in NVS `cfg/device_id` (random via
`esp_fill_random`; one-time STA-MAC seed when upgrading a device that already has WiFi/MQTT
config). Result is cached in RAM. Pure helpers in `device_identity_pure.h` cover create-mode
selection and hex formatting. Formatting helpers turn the ID into the unique STA/DHCP/mDNS
hostname `chaya2mqtt-<deviceId>`; the setup AP keeps the unsuffixed hostname.

---

## `mqtt/config` – broker configuration

**Files:** `mqtt/config.h`, `mqtt/config.cpp`

The active `MqttConfig` is static in `config.cpp`. It is accessed only through API functions (protected by a mutex).

| Function | Description |
|----------|-------------|
| `loadMQTTConfig()` / `saveMQTTConfig()` | Read/write NVS `mqtt` |
| `mqttCfgSnapshot(MqttConfig*)` | Thread-safe copy |
| `mqttCfgStorePending(...)` | Web form → pending |
| `mqttCfgApplyPendingToActive()` | Pending → Active |
| `mqttCfgApplyPairingTopics(MqttConfig*)` | Derive topics from own ID + partner ID (without a partner: empty subscribe topic) |

Sanitization when loading NVS: invalid servers/topics/partner IDs are cleaned up.

---

## `mqtt/mqtt` – MQTT client

**Files:** `mqtt/mqtt.h`, `mqtt/mqtt_internal.h`, `mqtt/mqtt_config.h`, `mqtt/mqtt_timing.h`, `mqtt/mqtt_client.cpp`, `mqtt/mqtt_events.cpp`, `mqtt/mqtt_publish.cpp`, `mqtt/mqtt_reconnect.cpp`

| File | Responsibility |
|------|----------------|
| `mqtt_client.cpp` | Client allocation, TLS, mutex |
| `mqtt_events.cpp` | Event handler, subscribe, payload parsing |
| `mqtt_publish.cpp` | Chaya publish, settings-application block |
| `mqtt_reconnect.cpp` | `mqttLoop()`, prechecks, backoff |

ESP-IDF `esp_mqtt_client` over `mqtts://` with a TLS bundle (`tls/`).

| Function | Description |
|----------|-------------|
| `mqttSetup()` | Reset client and backoff |
| `mqttLoop()` | Reconnect logic, prechecks, client initialization |
| `mqttDisconnect()` | Stop and destroy client |
| `chayaRequestSend()` | Single send entry (button + web): guards + LED TX sequence → publish |
| `mqttPublishChayaAndApplySentCounters()` | Start retained QoS 1 publish; counters apply on PUBACK (network task does not wait) |
| `mqttIsConnected()` | Connection status |
| `mqttPublishBlocked()` | True while applying settings |
| `mqttBeginSettingsApply()` / `mqttEndSettingsApply()` | Publishing lock |

Event handler (`MQTT_EVENT_DATA`): parse payload → `heartCounterStoreFromRemote()` → RX click + LED pulse → redraw display. A matching `MQTT_EVENT_PUBLISHED` applies the TX counter exactly once and queues a TX click.

---

## `wifi/wlan` – WiFi & captive portal

**Files:** `wifi/wlan.h`, `wifi/wlan_config.h`, `wifi/wlan_internal.h`, `wifi/wlan.cpp`, `wifi/wlan_reset.cpp`, `wifi/wlan_boot.cpp`, `wifi/wlan_events.cpp`, `wifi/wlan_nvs.cpp`, `wifi/wlan_scan.cpp`

| File | Responsibility |
|------|----------------|
| `wlan.cpp` | Global state, `wlanLoop()`, SoftAP snapshot, API lock, STA snapshots |
| `wlan_reset.cpp` | Factory reset, controlled restart, forced STA reassociation |
| `wlan_boot.cpp` | `setupWiFi()`, STA/AP fallback (WPA2/WPA3 setup AP), mDNS/NTP |
| `wlan_events.cpp` | STA events, reconnect backoff |
| `wlan_recovery.cpp` / `wlan_recovery.h` | Stage 2 recovery (forced reassociation / restart with OTA guard) |
| `wlan_nvs.cpp` | NVS WiFi configuration (packed `cfg_v2`, migration from `cred_v1`) |
| `wlan_scan.cpp` | Scan cache, refresh |

| Function | Description |
|----------|-------------|
| `setupWiFi()` | Register routes, start STA or `Chaya2MQTT` AP, start server |
| `wlanLoop()` | Captive DNS, mDNS restart, WiFi scan service, recovery |
| `wlanRecoveryServiceLoop()` | Forced reassociation after an extended STA outage; restart with guards |
| `wlanApSetupSnapshot(...)` | SoftAP SSID and IP for display and API |
| `wlanApSetupPassSnapshot(...)` | SoftAP WPA-PSK for WIFI QR payload (not shown as plain text) |
| `wlanSaveConfigToNvs(...)` | Write NVS `wifi` (packed `cfg_v2`: DHCP/static, DNS, NTP) |
| `configSaveWiFiCredentials(...)` | Compatibility wrapper: stores a DHCP-only configuration |
| `configIsApMode()` | SoftAP setup mode? |
| `resetAllSettings()` | Factory reset: delete NVS, restart |
| `wlanStaConnectedOk()` | STA connected + IP? |
| `wlanStaStableForMqtt()` | STA stable ≥3 s after GOT_IP? |
| `wlanNtpSynced()` | Is the NTP time plausible? |
| `wlanSetStaPowerSaveMqttActive(bool)` | MQTT up → `WIFI_PS_MIN_MODEM`; MQTT down/reconnect → `WIFI_PS_NONE` |
| `wlanForceStaReassoc` / `wlanControlledRestart` | Shared forced reassociation and restart paths |
| `wlanBootSettledAtMs` | Timestamp for the OTA health window |
| `wlanWifiScanCopySnapshot(...)` | Scan results (maximum 40 APs) |
| `wlanHandleStaReconnectNetCmd()` | Reconnect with backoff |

### `wifi/test` – connection test

**Files:** `wifi/test.h`, `wifi/test.cpp`

AP mode: tests the STA connection before saving credentials.

---

## `network/network_task` – network orchestration

**Files:** `network/network_task.h`, `network/network_task.cpp`

Network task (7168 stack, priority 5, core 1):
- Process the `NetCmd` queue (`kNetworkPollApMs` = 50 ms in SoftAP, `kNetworkPollStaMs` = 250 ms in STA)
- Run `wlanLoop()` every cycle
- Run `mqttLoop()` only in STA mode

---

## `display/` – E-Ink

**Files:** `display/display.h`, `display/display_config.h`, `display/display.cpp`, `display/display_hw.cpp`, `display/display_task.cpp`, `display/display_task_internal.h`, `display/draw.cpp`, `display/internal.h`, `display/qr/qrcodegen.{c,h}`

### Display task

Only this task may access SPI/EPD. Commands are sent through `g_displayCmdQueue`.
If a heart redraw cannot be queued, one pending redraw is coalesced and retried with current
counters after the active display command. Splash and power-off commands keep priority.

| Function | Description |
|----------|-------------|
| `displayInit()` | Initialize SPI + EPD |
| `displayStartTask()` | FreeRTOS task (8192 stack, priority 3) |
| `drawHeartWithNumber(icon)` | Lucide heart / heart-crack + RX/TX deltas + arrows + battery |
| `drawSplashScreen()` | SoftAP: red title + WIFI QR with equal top/bottom frame pads for phone camera join |
| `displayRequest(cmd, mode, waitMs)` | Single entry: Content / BootIfChanged / PowerOffWait |
| `displayWaitDrawIdle()` | Wait until the display task finishes the next draw |

`DisplayRequestMode::Content` — heart content redraw (`waitMs` 100 default, `0` non-blocking). `BootIfChanged` — splash or heart after setup (skip if NVS view matches). `PowerOffWait` — shutdown screen and wait for that refresh.

Geometry details: [DISPLAY.md](DISPLAY.md)

### GxEPD2 (PlatformIO)

**Dependency:** `ZinggJM/GxEPD2` in `platformio.ini`

**Type:** `ChayaEpdPanel` via `GxEPD2_4C` / `GxEPD2_154c_GDEM0154F51H` (`display/internal.h`) for the onboard 1.54G panel.

- Hardware: Waveshare 1.54G, 200×200, black/white/red/yellow ([HARDWARE.md](HARDWARE.md))
- SPI: SCLK 12, MOSI 13, CS 11, DC 10, RST 9, BUSY 8; panel power GPIO6 (active-low)
- Full-window refresh (~20 s)

---

## `battery/` – ADC + controlled power-off

**Files:** `battery/battery.h`, `battery/battery.cpp`, `battery/battery_pure.h`, `battery/battery_config.h`

GPIO4 ADC, `VBAT = VADC × 2`, averaged in the app task about every 30 s. Always treated as a LiPo. `batteryPowerOffAndSleep()` arms active-low PWR wake, drives `kBatControl` LOW, and enters deep sleep if USB still powers the ESP32. ETA6098 charge termination/recharge is autonomous and is not controlled from firmware.

## `hw/` – power-save hold-off + pin map

**Files:** `hw/sd_hold.h`, `hw/sd_hold.cpp`, `hw/pins.h`, `hw/pins_esp32_waveshare.h`

`sdHoldOff()` at boot drives SD CLK/DAT0/CMD (GPIO 39/40/41) OUTPUT LOW. No SDIO/FAT driver is started. Pin map is shared board GPIO definitions for hold-offs and other hardware themes.

## `audio/` – ES8311 playback

**Files:** `audio/audio.h`, `audio/audio.cpp`, `audio/audio_pure.h`, `audio/audio_config.h`

Dedicated task + `g_audioCmdQueue`. Capture/mic path is disabled at boot. Playback on TX/RX heart events (per-kind enable, volume, quiet hours; default both kinds off). Sine clicks use NVS `snd_tx_hz`/`snd_tx_ms`/`snd_tx_vol` and `snd_rx_hz`/`snd_rx_ms`/`snd_rx_vol` (defaults 880 Hz/80 ms/70 and 660 Hz/140 ms/70).
Queue overflow sets separate TX/RX pending flags; each kind is replayed at most once per drain
cycle, so bursts do not grow an unbounded audio backlog.

## `button/` – BOOT, PWR latch

**Files:** `button/button.h`, `button/button_config.h`, `button/button_internal.h`, `button/button_debounce_pure.h`, `button/button_input.cpp`

| File | Responsibility |
|------|----------------|
| `button_input.cpp` | BOOT GPIO/ISR and debounce; PWR arms soft-off after ≥2 s (LED ack via `led/`), runs shutdown on release |

| Constant | Value | Meaning |
|----------|-------|---------|
| Heart button | GPIO 0 (BOOT) | After boot; do not hold during flash |
| `BAT_Control` | GPIO 17 | Drive HIGH at boot on battery |
| `BAT_KEY` / PWR | GPIO 18 | Battery power button |
| `kShortPressMinMs` | 50 | Minimum short-press duration |

Button task (4096 stack, priority 8, core 1):
- Debounce (~20 ms)
- BOOT press → `chayaRequestSend()` (same path as web send)
- Advances LED state machine each poll (`advanceLedSequence`)
- No physical factory-reset gesture; reset remains available through the web admin

| Function | Description |
|----------|-------------|
| `buttonInit()` | Initialize GPIO (+ `ledInit()`) |
| `buttonStartTask()` | Start FreeRTOS task |
| `buttonStartupBlink()` | Boot preset 3× 200/200 ms (blocking, setup only) |
| `buttonNotifyTask()` | Wake button task (used by `led/`) |

## `led/` – header user LED

**Files:** `led/led.h`, `led/led.cpp`, `led/led_config.h`, `led/led_internal.h`, `led/led_pattern_pure.h`

Optional green header LED (GPIO 3, active-low); charge LED is separate (red, hardware-only).

LED priority: MQTT TX sequence > finite pattern > E-Ink/RX refresh pulse > idle.

| Function | Description |
|----------|-------------|
| `ledInit()` | Configure GPIO |
| `ledApplyEnabled()` | Force off when user disabled LED in settings |
| `ledEnableGpioHoldForLightSleep()` | Hold LED level for light sleep |
| `ledIsActivityActive()` | TX sequence, pattern, or refresh pulse running? |
| `ledIsTxSendBusy()` | MQTT TX send sequence running (blocks a second send) |
| `ledStartChayaSendSequence()` | Arm TX sequence (prefer `chayaRequestSend()`) |
| `ledRefreshPulseBegin/End` | Pulse GPIO3 during E-Ink refresh / RX ack |
| `ledPlayPattern` / `ledPlayPreset` | Queue finite blink or Boot/WifiUp/MqttUp/LinkDown/SoftOff |
| `ledPlayPatternBlocking` / `ledPlayPresetBlocking` | Blocking blink (boot / soft-off ack; same timings as queued) |

Presets: Boot (startup), WifiUp (STA ready / reconnect), MqttUp (broker connected), LinkDown (heart → crack), SoftOff (PWR armed).

---

## `web/` – admin interface

| File | Purpose |
|------|---------|
| `admin.h` / `admin.cpp` | Server singleton, route registration, `webAdminLoop()` |
| `admin_globals.h` / `admin_globals.cpp` | Shared atomics/flags; `adminApplyOptional*` form helpers |
| `admin_json.h` | JSON helper for small responses |
| `deferred_reboot.h` / `deferred_reboot.cpp` | Reboot after saving WiFi |
| `web_utils.h` / `web_utils.cpp` | Redirects, security headers |
| `web_middleware.h` / `web_middleware.cpp` | Host/CSRF middleware for API routes |
| `csrf.h` / `csrf.cpp` | Generate and validate CSRF tokens |
| `events.h` / `events.cpp` | SSE `/events` |
| `routes/admin_routes_api.cpp` | Register entry + shared `sendOk`/`sendErr` |
| `routes/admin_routes_api_*.cpp` | Thematic JSON API `/api/*` (device, chaya, wifi, mqtt, settings, system, ota) |
| `routes/admin_routes_captive.cpp` | Captive-portal probes, redirects, and setup entry points |
| `routes/admin_routes_spa.cpp` | Generic SPA blob lookup + SPA fallback |
| `spa_asset_lookup.h` | Path/MIME/cache helpers (natively testable) |
| `assets/web_ui.*` | Generated gzip asset blob, `.incbin` stub, and manifest (+ index HTML literal) |

Details: [WEB_ADMIN.md](WEB_ADMIN.md)

---

## `ota/` – firmware updates

| File | Purpose |
|------|---------|
| `ota.h` / `ota.cpp` | Automatic check logic, download queue |
| `ota_task.cpp` | OTA task (12288 stack, priority 4) |
| `github.h` / `github.cpp` | GitHub Releases API, CalVer comparison |
| `flash.h` / `flash.cpp` | TLS + SHA-256 sidecar, Arduino `HTTPUpdate` |
| `version_cmp.h` | CalVer/beta (`-rc.N`) comparison (header-only) |
| `github_parse.h` | GitHub release JSON helper (header-only) |

| Function | Description |
|----------|-------------|
| `otaLoop()` | Daily automatic check + pending download |
| `otaQueueGithubCheck()` | Trigger a manual check |

Details: [OTA.md](OTA.md)

---

## `config/` – application configuration

### `config/app_config`

| Function | Description |
|----------|-------------|
| `configGetResetPeriodDays()` | 0=off, 1–30 days (default 7) |
| `configSetResetPeriodDays(uint8_t)` | NVS `cfg/rstPeriod` |
| `configLoadUiPrefsFromNvs()` / language / theme | NVS `cfg/ui_lang`, `cfg/ui_theme` |
| `configLoadLedFromNvs()` / LED enable | NVS `cfg/led_en` |
| `configGetAudioTxEnabled()` / `configGetAudioRxEnabled()` / TX/RX volume / quiet hours | NVS `cfg/snd_*` |

### `config/nvs_utils`

Thread-safe `Preferences` wrapper with `g_nvsMutex`:
- `readInt`, `writeInt`, `readUInt`, `writeUInt`, `readUChar`, `writeUChar`
- `clearNamespace(const char*)`

---

## `diag/` – runtime monitoring

| File | Purpose |
|------|---------|
| `task_watchdog.h` | `chayaTaskWatchdogSubscribe()` / `Unsubscribe()` / `Reset()` |
| `stack_monitor.h` | Periodic stack high-water logging |

---

## `tls/` – TLS CA bundle

| File | Purpose |
|------|---------|
| `tls/tls_bundle.h` | Embedded X.509 CA certificates (PROGMEM) |
| `tls/tls_bundle_setup.h` / `tls_bundle_setup.cpp` | One-time CA bundle initialization (mutex, used by MQTT + OTA) |

---

## Utility modules

| File | Purpose |
|------|---------|
| `constants.h` | Device-wide identity, NTP, syntax validation (cross-module) |
| `mqtt/mqtt_config.h` | MQTT protocol defaults (topic prefix, port, keepalive, outbox) |
| `mqtt/mqtt_timing.h` | MQTT backoff, lock timeouts |
| `wifi/wlan_config.h` | WiFi limits, connection tuning, scan/reconnect intervals |
| `display/display_config.h` | Display limits (`kDisplayCounterMax`) |
| `led/led_config.h` | LED timing |
| `button/button_config.h` | Button debounce / soft-off timing |
| `config/version.h` | `APP_VERSION` (release workflow sets it from the Git tag) |
| `util/log_tag.h` | `DEFINE_LOG_TAG` macro |
| `util/ip_format.h` | IP address formatting |
| `util/time_helpers.h` | Wrap-safe time helpers (`elapsedMs`, `deadlineReached`, `remainingMs`) |
| `config/nvs_keys.h` | Central NVS namespace and key constants |
| `async/task_config.h` | FreeRTOS task stack sizes and queue depths |

### Logging convention

- Always use `DEFINE_LOG_TAG("…")` + `ESP_LOGx(TAG, …)` from `esp_log.h`. Do **not** use Arduino `log_e`/`log_i` (fixed tag) or `Serial.printf`.
- Levels: `E` = failure, `W` = unexpected but continuing, `I` = state transition, `D` = detail/defer (visible when `CORE_DEBUG_LEVEL≥4`).
- Dev (`esp32s3`): `CORE_DEBUG_LEVEL=4` plus IDF tags `wifi`/`mqtt_client` at DEBUG and `esp-tls`/`transport_base` at INFO (set in `main.cpp`).
- Release (`esp32s3-release`): `CORE_DEBUG_LEVEL=0` (logs compiled out / silent).

---

## Cross-references

- Architecture: [ARCHITECTURE.md](ARCHITECTURE.md)
- MQTT: [MQTT.md](MQTT.md)
- Web-Admin: [WEB_ADMIN.md](WEB_ADMIN.md)
- Hardware: [HARDWARE.md](HARDWARE.md)
- Configuration: [CONFIGURATION.md](CONFIGURATION.md)
