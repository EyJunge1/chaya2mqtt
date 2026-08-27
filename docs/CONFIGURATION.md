# Configuration & NVS

All persistent settings are stored in the ESP32-S3 **NVS** (Non-Volatile Storage). The firmware uses four namespaces through `Preferences` (thread-safe via `g_nvsMutex` in `config/nvs_utils`).

## NVS namespaces

| Namespace | Module | Description |
|-----------|--------|-------------|
| `wifi` | `wifi/wlan_*.cpp` | WiFi credentials, IP/DNS/NTP, SoftAP PIN |
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
| `ap_pin` | String | 8-digit SoftAP PIN for WIFI QR (created on first setup AP; survives STA save) |

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

**Written by:** `wlanSaveConfigToNvs()`—from web POST `/api/wifi/connect` (STA) or `/api/wifi/connect-commit` (AP test)

The device ID and hostnames are not stored in NVS. The ID is derived from the STA MAC at runtime; the setup hostname is `chaya2mqtt`, while the LAN hostname is `chaya2mqtt-<deviceId>`. They therefore remain stable across configuration changes and factory reset without a migration.

## Namespace `mqtt`

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `server` | String | `""` | Broker hostname or IP |
| `port` | Int | `8883` | MQTT port |
| `user` | String | `""` | MQTT username |
| `pass` | String | `""` | MQTT password |
| `partner_id` | String | `""` | Partner device ID (6 hexadecimal characters) |

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
| `rstPeriod` | UChar | `7` | Display reset period in UTC days (0=off, 1–30) |
| `ui_lang` | String | `en` | UI language (`en` / `de`) |
| `ui_theme` | String | `light` | Web UI theme (`light` / `dark`) |
| `led_en` | UChar | `1` | Header user LED (`1`=activity blinks, `0`=off) |
| `disp_view` | UChar | `0` | Last painted E-Ink view (`0`=unknown, `1`=heart, `2`=setup QR, `3`=product title) |
| `snd_mute` | UChar | `0` | Mute TX/RX click (`1` = no sound) |
| `snd_vol` | UChar | `70` | Click volume 0–100 |
| `snd_q0` | UChar | `23` | Quiet-hours start (local hour after NTP) |
| `snd_q1` | UChar | `8` | Quiet-hours end (equal to `snd_q0` = off; wraps midnight) |
| `snd_custom` | UChar | `0` | Use custom TX/RX Hz/ms (`1`); otherwise built-in tone |
| `snd_tx_hz` | UInt | `95` | Send click frequency (Hz, 40–2000) |
| `snd_tx_ms` | UInt | `80` | Send click duration (ms, 20–500) |
| `snd_rx_hz` | UInt | `88` | Receive click frequency (Hz, 40–2000) |
| `snd_rx_ms` | UInt | `140` | Receive click duration (ms, 20–500) |
| `upd_day` | UInt | `0` | Last automatic OTA check (UTC calendar day) |
| `upd_chan` | String | `stable` | OTA channel (`stable` or `beta`) |

**Written by:**
- `rstPeriod` / `ui_lang` / `ui_theme` / `led_en` / `snd_mute` / `snd_vol` / `snd_q0` / `snd_q1` / `snd_custom` / `snd_tx_hz` / `snd_tx_ms` / `snd_rx_hz` / `snd_rx_ms`: web POST `/api/settings` (deferred via the app task)
- `disp_view`: by the display task after a completed full refresh; unchanged values are not rewritten
- `upd_day`: automatically after an OTA check
- `upd_chan`: when selecting a channel during the update check

Note: Older firmware versions could set `cfg/authEn` and `cfg/disp_dark`; these keys are ignored.

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

**Baseline persistence:** `persistCounterBaselineState()` writes only `baseBlob` (`ChayaBaselineBlob`: two `int32_t` + one `uint32_t`). On load, `baseBlob` is preferred; if missing or the wrong size, the three legacy keys are read.

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
| `s_audioMutedCached` / volume / quiet hours | `cfg/snd_*` | app_config |

The active MQTT configuration (`mqttCfg`) exists only in `mqtt/config.cpp`—access is through the snapshot/pending API.

## Factory reset

Trigger: web admin **Settings → Device → Factory reset** (`POST /api/factory-reset`) →
`resetAllSettings()` in `wifi/wlan.cpp`. There is no physical reset gesture; if the web
admin is unreachable, erase and reflash over USB.

Sequence:
1. `g_systemShutdownInProgress = true`
2. Suspend NVS saves for counters
3. Abort the WiFi test
4. Stop the HTTP server and terminate DNS/mDNS
5. Disconnect WiFi
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
| `hw/button_config.h` | Button debounce, soft-off and LED timing |
| `async/task_config.h` | Task stacks, queue depths |

## Further documentation

- MQTT configuration details: [MQTT.md](MQTT.md)
- Web routes: [WEB_ADMIN.md](WEB_ADMIN.md)
- Counter logic: [DISPLAY.md](DISPLAY.md)
