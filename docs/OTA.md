# OTA – Firmware Updates

Chaya2MQTT supports **over-the-air updates** through GitHub Releases. The firmware is downloaded via HTTPS, checked against a SHA-256 sidecar, and flashed to the next OTA partition using Arduino `HTTPUpdate`.

**USB / first install** uses a separate **factory** image (`firmware.factory.bin`) via PlatformIO or the [web flasher](../flasher/README.md). That package includes bootloader, partition table, and app. OTA must **never** flash the factory image into an OTA slot—only `firmware.bin`.

## Overview

```mermaid
flowchart LR
    trigger[Automatic check or manual]
    github[GitHub Releases API]
    calver[CalVer comparison]
    confirm[UI confirmation]
    sha256[SHA-256-Sidecar]
    httpUpdate[HTTPUpdate]
    reboot[Controlled reboot]

    trigger --> github
    github --> calver
    calver -->|newer| confirm
    confirm --> sha256
    sha256 --> httpUpdate
    httpUpdate --> reboot
```

## Channels

| Channel | Source | Selection |
|---------|--------|-----------|
| `stable` | `/repos/.../releases/latest` | Latest non-draft release |
| `beta` | Paginated `/repos/.../releases?per_page=1&page=N` | Highest CalVer prerelease, otherwise stable; maximum 20 release pages |

The channel selection is stored in NVS (`cfg/upd_chan`). Automatic downgrades are not performed.

## GitHub release source

| Parameter | Value |
|-----------|-------|
| Repository | `EyJunge1/chaya2mqtt` |
| Firmware URL (OTA) | `https://github.com/EyJunge1/chaya2mqtt/releases/download/{tag}/firmware.bin` |
| SHA-256 URL (OTA) | `https://github.com/EyJunge1/chaya2mqtt/releases/download/{tag}/firmware.sha256` |
| Factory URL (USB / web flasher) | `https://github.com/EyJunge1/chaya2mqtt/releases/download/{tag}/firmware.factory.bin` |

OTA URLs are checked both when constructed and immediately before flashing: HTTPS, the exact
`EyJunge1/chaya2mqtt` release path, a strict CalVer tag, and exactly `firmware.bin` or
`firmware.sha256` are required. Redirect handling remains TLS-validated; a factory filename,
different host/repository, query suffix, or path traversal is rejected before `HTTPUpdate`.

Releases are triggered **manually** by a Git tag (CI: `.github/workflows/build-release.yml`). The tag must point to a commit contained in `main`.

### SHA-256 support

The pinned Arduino-ESP32 framework includes [arduino-esp32#12824](https://github.com/espressif/arduino-esp32/pull/12824) and [arduino-esp32#12848](https://github.com/espressif/arduino-esp32/pull/12848). OTA passes the published sidecar URL to `HTTPUpdate::setSHA256sumUrl()`; Arduino downloads and validates the 64-character hexadecimal hash before accepting the update.

## Versioning (CalVer)

Schema: **`YYYY.M.PATCH`** (month without a leading zero). Git tags have a `v` prefix.

| Type | Git tag | `APP_VERSION` in the firmware |
|------|---------|-------------------------------|
| Stable | `v2026.8.1` | `2026.8.1` |
| Beta | `v2026.8.1-rc.1` | `2026.8.1-rc.1` |

Beta releases use the CalVer suffix `-rc.N` in tags. They are published on GitHub as **prereleases** and are not marked “Latest.” The stable channel therefore does not see them; the beta channel prefers them.

### Creating and pushing a tag

```bash
# Beta (from main; tag suffix remains -rc.N)
git checkout main
git pull --no-tags origin main
git tag -a v2026.8.1-rc.1 -m "Beta 2026.8.1-rc.1"
git push origin v2026.8.1-rc.1

# Stable (from main)
git tag -a v2026.8.1 -m "Release 2026.8.1"
git push origin v2026.8.1
```

Before tagging, run locally: `make check`.

## Version comparison

- `APP_VERSION` from `config/version.h` (CI sets it from the tag **without** the leading `v`, e.g. `2026.8.1`)
- GitHub `tag_name` is compared component by component as CalVer (`YYYY`, month, patch, beta `-rc.N` number); stable sorts above beta with the same base version
- An upgrade is available if the remote version > local version
- `APP_VERSION == "dev"`: **no automatic check** (manual only)

## Automatic check (daily)

| Condition | Description |
|-----------|-------------|
| Mode | STA mode only (not AP) |
| WiFi | STA connected |
| NTP | Time synchronized (`> 1700000000` UTC) |
| Version | `APP_VERSION != "dev"` |
| Interval | At most once per UTC calendar day |
| NVS key | `cfg/upd_day` (last check day) |

Sequence:
1. Check the calendar day → if already checked: skip
2. `otaGithubEvaluateChannel()` → CalVer comparison for the selected channel
3. On upgrade: set status to `available` (**no** automatic installation)
4. Store the check day in NVS

## Manual check / installation

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/update/status` | GET | Status snapshot |
| `/api/update/check` | POST | Start check; optional `channel=stable\|beta` |
| `/api/update/install` | POST | Start installation after confirmation |

The `ota` SSE event provides live updates (phase, progress, error).

Web UI: `/update`—channel selection, version display, progress, and a confirmation dialog before installation.

## Download & installation

`otaFlashVerifiedInstall(binUrl, sha256Url)` in `ota/flash.cpp`:

1. **Configure sidecar:** pass `firmware.sha256` to `setSHA256sumUrl()`
2. **HTTPUpdate:** download the sidecar and firmware via HTTPS + TLS (CA bundle), redirects enabled, `rebootOnUpdate(false)`
3. **Flash:** Arduino `HTTPUpdate`/`Update` writes to the next OTA partition
4. **Progress:** callbacks update the OTA status for SSE/UI
5. **Reboot:** controlled by chaya2mqtt after flushing

On a SHA-256 mismatch, sidecar error, or flash error: **no reboot**.

## OTA task

| Parameter | Value |
|-----------|-------|
| Stack | 8192 bytes |
| Priority | 4 |
| Core | 1 |
| WDT | Registered (temporarily unregistered during `otaLoop()`) |

Before rebooting after a successful flash:
- `flushAllHeartCountersIfDirty()`
- `releaseGpioHoldBeforeRestart()`
- `ESP.restart()`

## Boot after OTA

In the **app task** (`appTaskFn`), rollback is canceled only after a **stable runtime window**:

- Helper: `otaHealthWindowElapsed()` in `src/ota/ota_health.h`
- Default: **`kOtaHealthStableMs = 30000`** (30 s) after the first WiFi boot settlement (`wlanBootSettledAtMs()`)
- Requirements: `wlanIsSetupComplete()` and `wlanIsBootWifiSettled()` (STA **or** AP fallback)
- MQTT availability is **not** required (broker/router are external sources of failure)
- Then `otaTryMarkValidAfterHealthCheck()` marks the image as valid and cancels rollback

Pure helper tests: `test/test_ota/test_ota.cpp` (`test_ota_health_window`, including wraparound).

## CI/CD pipeline

GitHub Actions (`.github/workflows/build-release.yml`):

1. Trigger: push of tag `vYYYY.M.PATCH` or `vYYYY.M.PATCH-rc.N`
2. Validate tag format; commit must be an ancestor of `origin/main`
3. Set `APP_VERSION` from the tag (without `v`)
4. Run the complete `make check` quality gate, including frontend/flasher checks, tests, static analysis, and the release firmware build
5. Validate and package the built images via `scripts/prepare_release_artifacts.py`
6. Publish GitHub Release assets:
   - `firmware.bin` + `firmware.sha256` (OTA)
   - `firmware.factory.bin` + `firmware.factory.sha256` (USB / web flasher)
7. Beta tags (`-rc.N`) are published as prereleases

Web flasher Pages deploy (`.github/workflows/deploy-pages.yml`) runs only when the
repository is public and GitHub Pages is enabled. Before publishing Stable + Beta
factory images, it verifies each image against its `firmware.factory.sha256`
sidecar and checks the expected ESP image markers. Release preparation also
verifies that the factory app region is byte-identical to `firmware.bin`.

Local checks before commits: `make check`.

## Error handling

| Error | Behavior |
|-------|----------|
| GitHub API unavailable | Status `error` / `api_error`, no download |
| No upgrade available | Status `idle`, still store the check day |
| SHA-256 mismatch or invalid sidecar | Abort installation, no reboot |
| Flash error | Abort installation, no reboot |
| AP mode | Automatic check skipped |

## Security

- TLS with the Mozilla CA bundle for downloads
- SHA-256 integrity check against the release sidecar (not cryptographic proof of origin)
- **No code signature** for firmware blobs
- Threat model: trust in the GitHub release source
- During OTA, factory reset / reboot / network restart are blocked (`otaBlocksDestructiveAction`)

## USB recovery & core dumps

### Device no longer reachable (“brick”)

1. Prefer the [web flasher](../flasher/README.md) or PlatformIO USB recovery — see [HARDWARE.md](HARDWARE.md)
2. Connect USB-C on the 1.54G (hold **BOOT** only if the port does not enumerate)
3. Optionally erase flash (`pio run -e esp32s3-release -t erase` or erase in the web installer)
4. Flash the **factory** image (web flasher) or `make upload-erase`
5. The WPA2/WPA3 `Chaya2MQTT` SoftAP appears; the display shows a WIFI QR for phone camera join

There is **no** unauthenticated HTTP endpoint for core dumps.

### Reading a core dump

The `coredump` partition (64 KiB @ `0x790000` in `partitions_chaya_8mb.csv`):

```bash
# Read dump from flash (adjust port)
esptool.py --chip esp32s3 --port /dev/tty.usbmodem* read_flash 0x790000 0x10000 /tmp/coredump.bin

# Analyze against the matching ELF
pio run -e esp32s3-release   # creates .pio/build/esp32s3-release/firmware.elf
python3 scripts/analyze_coredump.py /tmp/coredump.bin esp32s3-release
```

## Further documentation

- Configuration (NVS `upd_day`, `upd_chan`): [CONFIGURATION.md](CONFIGURATION.md)
- Architecture (OTA task): [ARCHITECTURE.md](ARCHITECTURE.md)
- Hardware / brick recovery: [HARDWARE.md](HARDWARE.md)
