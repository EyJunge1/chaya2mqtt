# Code Reference (Modules)

Overview of all source modules under `src/`, with correct paths, responsibilities, and important APIs.

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
13. `buttonEnableLedGpioHoldForLightSleep()`

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

The TLS bundle and MQTT configuration modules own additional internal mutexes.

### `async/app_task.cpp`

App task (4096 stack, priority 4, core 1), loop every 500 ms:
- OTA health: after 30 s since WiFi boot settlement → `otaTryMarkValidAfterHealthCheck()`
- `webAdminLoop()`—deferred web work, SSE
- `maybePeriodicallyResetCounters()`—STA mode only
- `maybeResetDisplayBaselinesWhenCapped()`—STA mode only
- `maybeSaveHeartCounter()` / `maybeSaveHeartSentCounter()`
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
| `loadHeartCounter()` / `saveHeartCounter()` | Read/write NVS `chaya` |
| `persistCounterBaselineState()` | Write packed `baseBlob` (baselines + reset day) |
| `maybeSaveHeartCounter()` | Debounced save (≥30 s) |
| `flushHeartCounterIfDirty()` | Save immediately if changed |
| `maybePeriodicallyResetCounters()` | Periodic baseline roll (UTC days) |
| `maybeResetDisplayBaselinesWhenCapped()` | Baseline roll when display reaches ≥999 |
| `counterResetRamAfterFactoryClear()` | Reset RAM after factory reset |

NVS debouncing: saves only every **≥30 s** (`kHeartCounterSaveMinIntervalMs`).

---

## `device_identity` – stable device and network identity

**Files:** `device_identity.h`, `device_identity.cpp`, `device_identity_pure.h`

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
| `mqttPublishChayaAndApplySentCounters()` | Publish retained QoS 1; return after matching PUBACK or 5 s timeout |
| `mqttIsConnected()` | Connection status |
| `mqttPublishBlocked()` | True while applying settings |
| `mqttBeginSettingsApply()` / `mqttEndSettingsApply()` | Publishing lock |

Event handler (`MQTT_EVENT_DATA`): parse payload → `heartCounterStoreFromRemote()` → RX click + LED pulse → redraw display. A matching `MQTT_EVENT_PUBLISHED` applies the TX counter exactly once and queues a TX click.

---

## `wifi/wlan` – WiFi & captive portal

**Files:** `wifi/wlan.h`, `wifi/wlan_config.h`, `wifi/wlan_internal.h`, `wifi/wlan.cpp`, `wifi/wlan_boot.cpp`, `wifi/wlan_events.cpp`, `wifi/wlan_nvs.cpp`, `wifi/wlan_scan.cpp`

| File | Responsibility |
|------|----------------|
| `wlan.cpp` | Global state, `wlanLoop()`, factory reset, SoftAP snapshot, API lock |
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
| `wlanApSetupPassSnapshot(...)` | 8-digit SoftAP PIN (WIFI QR payload; not shown as plain text) |
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

**Files:** `display/display.h`, `display/display_config.h`, `display/display.cpp`, `display/draw.cpp`, `display/internal.h`, `display/qr/qrcodegen.{c,h}`

### Display task

Only this task may access SPI/EPD. Commands are sent through `g_displayCmdQueue`.
If a heart redraw cannot be queued, one pending redraw is coalesced and retried with current
counters after the active display command. Splash and power-off commands keep priority.

| Function | Description |
|----------|-------------|
| `displayInit()` | Initialize SPI + EPD |
| `displayStartTask()` | FreeRTOS task (8192 stack, priority 3) |
| `drawHeartWithNumber(icon)` | Lucide heart / heart-crack + RX/TX deltas + arrows + battery |
| `drawSplashScreen()` | SoftAP: red title + bottom-aligned WIFI QR for phone camera join |
| `requestHeartRedraw()` | Redraw heart (blocking, 100 ms queue timeout) |
| `requestHeartRedrawNonBlocking()` | Redraw heart (0 ms timeout, for MQTT callback) |
| `requestDeferredDrawSplashScreen()` | Queue SoftAP WIFI QR splash |
| `requestDeferredDrawHeartScreen()` | Heart after setup |
| `displayDrawPowerOffAndWait()` | Block normal redraws; queue and await the red shutdown title |

Geometry details: [DISPLAY.md](DISPLAY.md)

### GxEPD2 (PlatformIO)

**Dependency:** `ZinggJM/GxEPD2` in `platformio.ini`

**Type:** `ChayaEpdPanel` via `GxEPD2_4C` / `GxEPD2_154c_GDEM0154F51H` (`display/internal.h`) for the onboard 1.54G panel.

- Hardware: Waveshare 1.54G, 200×200, black/white/red/yellow ([HARDWARE.md](HARDWARE.md))
- SPI: SCLK 12, MOSI 13, CS 11, DC 10, RST 9, BUSY 8; panel power GPIO6 (active-low)
- Full-window refresh (~20 s)

---

## `hw/battery` – ADC + controlled power-off

**Files:** `hw/battery.h`, `hw/battery.cpp`, `hw/battery_pure.h`, `hw/battery_config.h`

GPIO4 ADC, `VBAT = VADC × 2`, averaged in the app task about every 30 s. Always treated as a LiPo. `batteryPowerOffAndSleep()` arms active-low PWR wake, drives `kBatControl` LOW, and enters deep sleep if USB still powers the ESP32. ETA6098 charge termination/recharge is autonomous and is not controlled from firmware.

## `hw/sd_hold` – microSD hold-off

**Files:** `hw/sd_hold.h`, `hw/sd_hold.cpp`

`sdHoldOff()` at boot drives SD CLK/DAT0/CMD (GPIO 39/40/41) OUTPUT LOW. No SDIO/FAT driver is started.

## `audio/` – ES8311 playback

**Files:** `audio/audio.h`, `audio/audio.cpp`, `audio/audio_pure.h`, `audio/audio_config.h`

Dedicated task + `g_audioCmdQueue`. Capture/mic path is disabled at boot. Playback on TX/RX heart events (mute, volume, quiet hours). Built-in sine clicks by default; optional custom Hz/ms via NVS (`snd_custom`, `snd_tx_hz`/`snd_tx_ms`, `snd_rx_hz`/`snd_rx_ms`).
Queue overflow sets separate TX/RX pending flags; each kind is replayed at most once per drain
cycle, so bursts do not grow an unbounded audio backlog.

## `hw/button` – BOOT, PWR latch, optional LED

**Files:** `hw/button.h`, `hw/button_config.h`, `hw/button_internal.h`, `hw/button_input.cpp`, `hw/button_led.cpp`, `hw/led_pattern_pure.h`, `hw/pins.h`

| File | Responsibility |
|------|----------------|
| `button_input.cpp` | BOOT GPIO/ISR and debounce; PWR long-press waits for shutdown draw, release, then powers off |
| `button_led.cpp` | TX LED sequence, refresh pulse, finite status patterns (`ledPlayPattern` / presets) |
| `led_pattern_pure.h` | Host-testable blink phase advance |

| Constant | Value | Meaning |
|----------|-------|---------|
| Heart button | GPIO 0 (BOOT) | After boot; do not hold during flash |
| `BAT_Control` | GPIO 17 | Drive HIGH at boot on battery |
| `BAT_KEY` / PWR | GPIO 18 | Battery power button |
| Optional LED | GPIO 3 | Green header user LED (active-low); charge LED is separate (red, hardware-only) |
| `kShortPressMinMs` | 50 | Minimum short-press duration |

Button task (4096 stack, priority 8, core 1):
- Debounce (~20 ms)
- BOOT press → MQTT send (optional LED blink → publish → blink)
- No physical factory-reset gesture; reset remains available through the web admin

LED priority: MQTT TX sequence > finite pattern > E-Ink/RX refresh pulse > idle.

| Function | Description |
|----------|-------------|
| `buttonInit()` | Initialize GPIO |
| `buttonStartTask()` | Start FreeRTOS task |
| `buttonStartupBlink()` | Boot preset 3× 200/200 ms (blocking, setup only) |
| `buttonIsLedTxSequenceActive()` | TX sequence, pattern, or refresh pulse running? |
| `ledRefreshPulseBegin/End` | Pulse GPIO3 during E-Ink refresh / RX ack |
| `ledPlayPattern` / `ledPlayPreset` | Queue finite blink (count, on/off ms) or Boot/WifiUp/MqttUp/LinkDown |

Presets: Boot (startup), WifiUp (STA ready / reconnect), MqttUp (broker connected), LinkDown (heart → crack).

---

## `web/` – admin interface

| File | Purpose |
|------|---------|
| `admin.h` / `admin.cpp` | Server singleton, route registration, `webAdminLoop()` |
| `admin_globals.h` / `admin_globals.cpp` | Shared atomics/flags |
| `admin_json.h` | JSON helper for small responses |
| `deferred_reboot.h` / `deferred_reboot.cpp` | Reboot after saving WiFi |
| `web_utils.h` / `web_utils.cpp` | Redirects, security headers |
| `web_middleware.h` / `web_middleware.cpp` | Host/CSRF middleware for API routes |
| `csrf.h` / `csrf.cpp` | Generate and validate CSRF tokens |
| `web_events.h` / `web_events.cpp` | SSE `/events` |
| `routes/admin_routes_api.cpp` | JSON API `/api/*` for the Svelte SPA |
| `routes/admin_routes_captive.cpp` | Captive-portal probes, redirects, and setup entry points |
| `routes/admin_routes_spa.cpp` | Generic SPA blob lookup + SPA fallback |
| `spa_asset_lookup.h` | Path/MIME/cache helpers (natively testable) |
| `assets/web_ui.*` | Generated asset blob (raw), `.incbin` stub, and manifest |

Details: [WEB_ADMIN.md](WEB_ADMIN.md)

---

## `ota/` – firmware updates

| File | Purpose |
|------|---------|
| `ota.h` / `ota.cpp` | Automatic check logic, download queue |
| `ota_task.cpp` | OTA task (8192 stack, priority 4) |
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
| `configGetAudioMuted()` / volume / quiet hours | NVS `cfg/snd_*` |

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
| `hw/button_config.h` | Button/LED timing |
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
