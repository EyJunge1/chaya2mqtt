# Code Reference (Modules)

Overview of all source modules under `src/`, with correct paths, responsibilities, and important APIs.

---

## `main.cpp`

**Purpose:** Bootstrap and task startup. No traditional Arduino `loop()` style.

**Setup sequence:**
1. `asyncInfraInit()`—queues + mutexes
2. CPU 240 MHz, BT off, DFS (no light sleep)
3. `displayInit()` + `displayStartTask()`
5. `buttonInit()`
6. Load NVS: MQTT, counters, reset period
7. `setupWiFi()`—STA or AP
8. `mqttSetup()`
9. `buttonStartupBlink()` (before the button task!)
10. `buttonStartTask()`, `networkTaskStart()`, `otaTaskStart()`, `appTaskStart()`
11. Deferred draw: heart or splash

**`loop()`:** `vTaskDelete(nullptr)`—terminates immediately.

---

## `async/` – infrastructure

### `async/event_types.h`

```cpp
enum class NetCmd : uint8_t {
    MqttSettingsChanged, MqttKillClient, WifiReconnect, ChayaSendRequested,
    FactoryResetRequested,
};

struct DisplayMsg {
    enum class Cmd : uint8_t { DrawHeart, DrawSplash };
    Cmd cmd;
    uint32_t payload;
};
```

### `async/task_handles.h` / `task_handles.cpp`

Global queues and mutexes. `asyncInfraInit()` creates:
- `g_netCmdQueue` (32 × `NetCmd`)
- `g_displayCmdQueue` (32 × `DisplayMsg`)
- 6 mutexes (see [ARCHITECTURE.md](ARCHITECTURE.md))

### `async/app_task.cpp`

App task (4096 stack, priority 4, core 1), loop every 500 ms:
- OTA health: after 30 s since WiFi boot settlement → `otaTryMarkValidAfterHealthCheck()`
- `webAdminLoop()`—deferred web work, SSE
- `maybePeriodicallyResetCounters()`—STA mode only
- `maybeResetDisplayBaselinesWhenCapped()`—STA mode only
- `maybeSaveHeartCounter()` / `maybeSaveHeartSentCounter()`
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
| `maybeSaveHeartCounter()` | Debounced save (≥30 s) |
| `flushHeartCounterIfDirty()` | Save immediately if changed |
| `maybePeriodicallyResetCounters()` | Periodic baseline roll (UTC days) |
| `maybeResetDisplayBaselinesWhenCapped()` | Baseline roll when display reaches ≥999 |
| `counterResetRamAfterFactoryClear()` | Reset RAM after factory reset |

NVS debouncing: saves only every **≥30 s** (`kHeartCounterSaveMinIntervalMs`).

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
| `buildDeviceId(char*, size_t)` | 6-character hexadecimal ID from MAC |
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
| `mqttPublishChaya()` | Publish `heartSentCounter + 1` (retained, QoS 0) |
| `mqttPublishChayaAndApplySentCounters()` | Publish + increment TX counter |
| `mqttIsConnected()` | Connection status |
| `mqttPublishBlocked()` | True while applying settings |
| `mqttBeginSettingsApply()` / `mqttEndSettingsApply()` | Publishing lock |

Event handler (`MQTT_EVENT_DATA`): parse payload → `heartCounterStoreFromRemote()` → redraw display.

---

## `wifi/wlan` – WiFi & captive portal

**Files:** `wifi/wlan.h`, `wifi/wlan_config.h`, `wifi/wlan_internal.h`, `wifi/wlan.cpp`, `wifi/wlan_boot.cpp`, `wifi/wlan_events.cpp`, `wifi/wlan_nvs.cpp`, `wifi/wlan_scan.cpp`

| File | Responsibility |
|------|----------------|
| `wlan.cpp` | Global state, `wlanLoop()`, factory reset, SoftAP snapshot, API lock |
| `wlan_boot.cpp` | `setupWiFi()`, STA/AP fallback (open setup AP), mDNS/NTP |
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
- Process the `NetCmd` queue (500 ms timeout)
- Run `wlanLoop()` every cycle
- Run `mqttLoop()` only in STA mode

---

## `display/` – E-Ink

**Files:** `display/display.h`, `display/display_config.h`, `display/display.cpp`, `display/draw.cpp`, `display/internal.h`

### Display task

Only this task may access SPI/EPD. Commands are sent through `g_displayCmdQueue`.

| Function | Description |
|----------|-------------|
| `displayInit()` | Initialize SPI + EPD |
| `displayStartTask()` | FreeRTOS task (4096 stack, priority 3) |
| `requestHeartRedraw()` | Redraw heart (blocking, 100 ms queue timeout) |
| `requestHeartRedrawNonBlocking()` | Redraw heart (0 ms timeout, for MQTT callback) |
| `requestDeferredDrawSplashScreen()` | Splash screen when the broker is missing |
| `requestDeferredDrawHeartScreen()` | Heart after setup |

Geometry details: [DISPLAY.md](DISPLAY.md)

### GxEPD2 (PlatformIO)

**Dependency:** `ZinggJM/GxEPD2` in `platformio.ini`

**Type in firmware:** `ChayaEpdPanel` = `GxEPD2_3C<GxEPD2_154_Z90c, GxEPD2_154_Z90c::HEIGHT>` (`display/internal.h`) — still the previous BWR panel class until the 1.54G 4-color port.

- Target hardware: Waveshare 1.54G, 200×200, black/white/red/yellow ([HARDWARE.md](HARDWARE.md))
- Current code: SSD1682 / GDEH0154Z90, 200×200, 3-color (BWR)
- Full-Window-Refresh only

---

## `hw/button` – button & LED

**Files:** `hw/button.h`, `hw/button_config.h`, `hw/button_internal.h`, `hw/button_input.cpp`, `hw/button_led.cpp`, `hw/pins.h`

| File | Responsibility |
|------|----------------|
| `button_input.cpp` | GPIO/ISR, debounce, factory reset → `NetCmd` |
| `button_led.cpp` | LED sequence, MQTT publish after blinking |

| Constant | Value | Meaning |
|----------|-------|---------|
| `kButtonGpio` | GPIO 2 | Button (`INPUT_PULLDOWN`) |
| `kButtonLedPin` | GPIO 4 | LED |
| `kFactoryResetHoldMs` | 10000 | Factory reset (hold for 10 s) |
| `kShortPressMinMs` | 50 | Minimum short-press duration |

Button task (4096 stack, priority 8, core 1):
- Debounce (~20 ms)
- Short press → MQTT send LED sequence (2× blink → publish → 2× blink)
- Hold for 10 s → `NetCmd::FactoryResetRequested` in the network task (`resetAllSettings()` is WDT-safe there)

| Function | Description |
|----------|-------------|
| `buttonInit()` | Initialize GPIO |
| `buttonStartTask()` | Start FreeRTOS task |
| `buttonStartupBlink()` | Blink 3× for 200 ms (blocking, setup only) |
| `buttonIsLedTxSequenceActive()` | Is the MQTT send sequence running? |

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
| `routes/admin_routes_api.cpp` | JSON API `/api/*` for the React SPA |
| `routes/admin_routes_spa.cpp` | Generic SPA blob lookup + SPA fallback |
| `spa_asset_lookup.h` | Path/MIME/cache helpers (natively testable) |
| `assets/web_ui.*` | Generated gzip blob, `.incbin` stub, and manifest |

Details: [WEB_ADMIN.md](WEB_ADMIN.md)

---

## `ota/` – firmware updates

| File | Purpose |
|------|---------|
| `ota.h` / `ota.cpp` | Automatic check logic, download queue |
| `ota_task.cpp` | OTA task (8192 stack, priority 4) |
| `github.h` / `github.cpp` | GitHub Releases API, CalVer comparison |
| `flash.h` / `flash.cpp` | TLS + MD5 sidecar, Arduino `HTTPUpdate` |
| `version_cmp.h` | CalVer/RC comparison (header-only) |
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
| `configGetDisplayDark()` / `configSetDisplayDark(bool)` | E-Ink dark mode, NVS `cfg/disp_dark` |

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

---

## Cross-references

- Architecture: [ARCHITECTURE.md](ARCHITECTURE.md)
- MQTT: [MQTT.md](MQTT.md)
- Web-Admin: [WEB_ADMIN.md](WEB_ADMIN.md)
- Hardware: [HARDWARE.md](HARDWARE.md)
- Configuration: [CONFIGURATION.md](CONFIGURATION.md)
