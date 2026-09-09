# Configuration & NVS

All persistent settings are stored in the ESP32-S3 **NVS** (Non-Volatile Storage). The firmware uses four namespaces through `Preferences` (thread-safe via `g_nvsMutex` in `config/nvs_utils`).

## NVS namespaces

| Namespace | Module | Description |
|-----------|--------|-------------|
| `wifi` | `wifi/wlan_*.cpp` | WiFi credentials, IP/DNS/NTP, SoftAP PSK |
| `mqtt` | `mqtt/config.cpp` | Broker configuration |
| `cfg` | `config/app_config.cpp`, `ota/ota.cpp` | App settings, OTA check day |
| `chaya` | `heart/counter_nvs.cpp` | Counters and baselines |

## Namespace `wifi`

| Key | Type | Description |
|-----|------|-------------|
| `cfg_v2` | Bytes (packed) | Current format: SSID, password, IP mode, static IPv4 fields, NTP |
| `cred_v1` | Bytes (packed) | Legacy: SSID + password only (migrated to DHCP when loaded) |
| `ssid` | String | Legacy format (fallback) |
| `pass` | String | Legacy format (fallback) |
| `ap_pin` | String | SoftAP WPA-PSK (24 alphanumeric) for WIFI QR (created on first setup AP; legacy 8-digit values are regenerated) |

When saving, legacy keys (`ssid`, `pass`, `cred_v1`) are removed and only `cfg_v2` is written.

### `cfg_v2` fields

| Field | Default | Description |
|-------|---------|-------------|
| Mode | `dhcp` | `dhcp` or `static` (manual) |
| IP / gateway / netmask | empty | Required for `static` |
| DNS1 / DNS2 | empty | Empty = DNS from DHCP; set = override (commonly Cloudflare `1.1.1.1` / `1.0.0.1`) |
| NTP1 / NTP2 | empty | Empty = automatic (DHCP option 42, otherwise `time.cloudflare.com`); set = override |

Invalid static fields in NVS are reset to DHCP when loaded. Known built-in NTP pairs are loaded as “automatic” (empty).
Static addresses are not coordinated between devices; every Chaya2MQTT on the same LAN must be assigned a different IP.

STA max TX power defaults to 52 quarter-dBm (13 dBm). During each E-Paper waveform the firmware
temporarily lowers that cap from the current RSSI (see `docs/DISPLAY.md`); the chosen target never
raises the configured maximum.

**Written by:** `wlanSaveConfigToNvs()`—from web POST `/api/wifi/connect` (STA) or `/api/wifi/connect-commit` (AP test)

The device ID is stored in NVS (`cfg/device_id`) as six lowercase hex characters. It is created
randomly on first boot and after factory reset / flash erase. On OTA upgrade from firmware that
had no `device_id` key, the ID is seeded once from the STA MAC when WiFi or MQTT config already
exists, so pairings and hostnames stay stable until the next reset. The setup SoftAP hostname is
`chaya2mqtt`; the LAN hostname is `chaya2mqtt-<deviceId>`.

## Namespace `mqtt`

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `server` | String | `""` | Broker hostname or IP |
| `port` | Int | `8883` | MQTT port (1883 plain / 8883 TLS typical) |
| `tls` | UChar | `1` | `1` = mqtts (TLS), `0` = mqtt (plain TCP); missing key defaults to TLS |
| `user` | String | `""` | MQTT username |
| `pass` | String | `""` | MQTT password |
| `partner_id` | String | `""` | Partner device ID (6 hexadecimal characters; required for heart display) |

Legacy keys `topic_pub` / `topic_sub` are removed when saving. Topics now exist only as derived values in RAM.

**Written by:** `saveMQTTConfig()`—after `mqttCfgApplyPendingToActive()` in the network task

### Sanitization when loading

- Invalid server → cleared
- Invalid partner ID → cleared
- Partner ID = own ID → cleared
- Topics are always derived: `chaya2mqtt/<own>`; subscribe only with a partner: `chaya2mqtt/<partner>`

## Namespace `cfg`

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `device_id` | String | (random) | Own 6-char hex device ID (MQTT topics, client ID, LAN hostname) |
| `rstPeriod` | UChar | `7` | Display reset period in UTC days (0=off, 1–30) |
| `ui_lang` | String | `en` | UI language (`en` / `de`) |
| `ui_theme` | String | `system` | Web UI theme (`system` / `light` / `dark`; appearance follows OS when `system`) |
| `led_en` | UChar | `1` | Header user LED (`1`=activity blinks, `0`=off) |
| `disp_view` | UChar | `0` | Last painted E-Ink view (`0`=unknown, `1`=heart, `2`=setup QR, `3`=product title, `4`=heart-crack, `5`=power-off) |
| `snd_tx_en` | UChar | `0` | TX (send) click enabled (`1` = on; default off) |
| `snd_rx_en` | UChar | `0` | RX (receive) click enabled (`1` = on; default off) |
| `snd_tx_vol` | UChar | `70` | TX click volume 0–100 |
| `snd_rx_vol` | UChar | `70` | RX click volume 0–100 |
| `snd_qB` | Bytes (2) | — | Packed quiet hours: start/end (current write format) |
| `snd_q0` | UChar | `0` | Legacy quiet-hours start (load fallback only) |
| `snd_q1` | UChar | `0` | Legacy quiet-hours end (equal to `snd_q0` = off; wraps midnight; load fallback only) |
| `snd_tB` | Bytes (8) | — | Packed tones: `txHz`, `txMs`, `rxHz`, `rxMs` (current write format) |
| `snd_tx_hz` | UInt | `880` | Legacy send click frequency (Hz, 40–2000; load fallback only) |
| `snd_tx_ms` | UInt | `80` | Legacy send click duration (ms, 20–500; load fallback only) |
| `snd_rx_hz` | UInt | `660` | Legacy receive click frequency (Hz, 40–2000; load fallback only) |
| `snd_rx_ms` | UInt | `140` | Legacy receive click duration (ms, 20–500; load fallback only) |
| `upd_day` | UInt | `0` | Last automatic OTA check (UTC calendar day) |
| `upd_chan` | String | `stable` | OTA channel (`stable` or `beta`) |

**Written by:**
- `device_id`: created by `buildDeviceId()` on first use (random, or one-time MAC seed on OTA migration)
- `rstPeriod` / `ui_lang` / `ui_theme` / `led_en` / `snd_tx_en` / `snd_rx_en` / `snd_tx_vol` / `snd_rx_vol` / `snd_qB` / `snd_tB`: web POST `/api/settings` (deferred via the app task)
- `disp_view`: two-phase display transaction—`Unknown` is persisted before a full refresh and the
  completed view afterward. A reset or power loss during the waveform therefore forces a repaint.
- `upd_day`: automatically after an OTA check
- `upd_chan`: when selecting a channel during the update check

Leftover keys from older firmware (`authEn`, `disp_dark`, `snd_custom`) are unused.
Legacy `cfg/snd_mute` is migrated once to `snd_tx_en` / `snd_rx_en` (`unmuted` → both on) when the new keys are absent.
Legacy `cfg/snd_vol` is migrated once to `snd_tx_vol` / `snd_rx_vol` when the new keys are absent.

**Audio persistence:** Quiet hours and tones are written as `snd_qB` / `snd_tB`. On load, a valid blob is used; a missing blob falls back to the legacy scalar keys; a present but wrong-size blob uses defaults, not legacy. After a successful blob write, the legacy keys are removed.

### Reset period (`rstPeriod`)

| Value | Behavior |
|-------|----------|
| `0` | Periodic reset disabled |
| `1`–`30` | Every N UTC days: set baselines to the current raw values |
| missing/invalid | Default: **7** days |

The periodic reset only resets the **display baselines** (the display returns to 0). The absolute MQTT counters (`heartCounter`, `heartSentCounter`) remain unchanged.

In addition, if a displayed delta reaches ≥ **999**, the baseline for that side is advanced immediately.

## Namespace `chaya`

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `counter` | Int | `0` | Received counter (absolute) |
| `sentCount` | Int | `0` | Sent counter (absolute) |
| `baseBlob` | Bytes (12) | — | Packed baselines: `cntBase`, `sntBase`, `rstDay` (current write format) |
| `cntBase` | Int | `0` | Legacy RX display baseline (load fallback only) |
| `sntBase` | Int | `0` | Legacy TX display baseline (load fallback only) |
| `rstDay` | UInt | `UINT32_MAX` | Legacy last periodic reset UTC day (load fallback only) |

**Baseline persistence:** `persistCounterBaselineState()` writes `baseBlob` (`ChayaBaselineBlob`: two `int32_t` + one `uint32_t`). On load, a valid blob is used; a missing blob falls back to the three legacy keys (`cntBase`, `sntBase`, `rstDay`); a present but wrong-size blob uses defaults (`0` / `0` / `UINT32_MAX`), not legacy. After a successful blob write, the legacy keys are removed.

**Storage strategy:**
- Counters (`counter` / `sentCount`): debounced save only every **≥30 s** if the value has changed
- Baselines: written immediately when the period rolls or a display cap reset advances them
- Flush before reboot/OTA: save counters immediately if dirty
- During factory reset: NVS writes are suspended

## RAM caches

Some values are additionally cached in RAM (atomics):

| Variable | Namespace key | Module |
|----------|---------------|--------|
| `heartCounter` | `chaya/counter` | counter |
| `heartSentCounter` | `chaya/sentCount` | counter |
| `counterBaseline` | `chaya/baseBlob` (legacy `cntBase`) | counter |
| `sentCountBaseline` | `chaya/baseBlob` (legacy `sntBase`) | counter |
| `s_resetPeriodDaysCached` | `cfg/rstPeriod` | app_config |
| `s_ledEnabledCached` | `cfg/led_en` | app_config |
| `s_displayViewCached` | `cfg/disp_view` | app_config |
| `s_audioTxEnabledCached` / `s_audioRxEnabledCached` / TX/RX volume / quiet hours | `cfg/snd_*` | app_config |

The active MQTT configuration (`mqttCfg`) exists only in `mqtt/config.cpp`—access is through the snapshot/pending API.

## Factory reset

Trigger: web admin **Settings → Device → Factory reset** (`POST /api/factory-reset`) →
`resetAllSettings()` in `wifi/wlan_reset.cpp`. There is no physical reset gesture; if the web
admin is unreachable, erase and reflash over USB.

`POST /api/factory-reset` returns `503 busy` when MQTT/settings apply is pending, an admin
reboot/Wi-Fi-save restart is armed, or OTA is busy; `503 shutdown` when shutdown or a factory
wipe is already queued.

Sequence:
1. HTTP sets `g_factoryResetQueued` (blocks Soft-off and admin restart) then queues `NetCmd::FactoryResetRequested`
2. Exclusive `systemShutdownTryClaim()`; abort without wipe if another owner already claimed or OTA is in progress
3. Suspend NVS saves for counters and wait for an in-flight EPD refresh (up to 90 s)
4. Stop the HTTP server only after the wipe is committed (so a refused claim does not leave HTTP down)
5. Abort the WiFi test, terminate DNS/mDNS, disconnect WiFi
6. **Delete all four namespaces:** `wifi`, `mqtt`, `cfg`, `chaya`
7. Reset RAM counters and configuration caches
8. Restart → SoftAP `Chaya2MQTT`

## Configuration changes through the web UI

| Setting | Route | Processing |
|---------|-------|------------|
| WiFi | POST `/api/wifi/connect` | Directly to NVS (STA) or test → commit (AP); fields: SSID/password, mode, optional IPv4/DNS/NTP |
| MQTT + pairing | POST `/api/mqtt` | Pending → app task → network task → NVS |
| Reset period / display / LED / sound | POST `/api/settings` | Pending → app task → NVS |

MQTT and settings changes are processed **as deferred work** (not in the HTTP handler) to avoid blocking.

## Constants headers

Module-specific defaults and limits are located in `*_config.h` (no longer centrally in `constants.h`):

| Header | Content |
|--------|---------|
| `constants.h` | Device identity, NTP, syntax validation |
| `mqtt/mqtt_config.h` | MQTT topic prefix, port, keepalive, outbox |
| `wifi/wlan_config.h` | SSID/password limits, STA tuning, scan/reconnect |
| `display/display_config.h` | `kDisplayCounterMax` |
| `button/button_config.h + led/led_config.h` | Button debounce, soft-off and LED timing |
| `async/task_config.h` | Task stacks, queue depths |

## Further documentation

- MQTT configuration details: [MQTT.md](MQTT.md)
- Web routes: [WEB_ADMIN.md](WEB_ADMIN.md)
- Counter logic: [DISPLAY.md](DISPLAY.md)
