# Display

The E-Ink display shows a **red heart** with **RX and TX counters** (delta display), plus yellow accents for a fresh receive and a low battery. All drawing takes place exclusively in the **display task**—other tasks send commands via `g_displayCmdQueue`. The user LED pulses for the duration of a heart/splash refresh.

## Display task

| Parameter | Value |
|-----------|-------|
| Stack | 8192 Bytes |
| Priority | 3 |
| Core | 1 |
| WDT | **Not registered** (a 1.54G full refresh can take ~20 s) |
| Boot initialization | `displayInit()` with `initial_full_refresh=true`—wakes the panel after hibernate, first draw is a full refresh |

### Commands (`DisplayMsg`)

| Command | Function | Trigger |
|---------|----------|---------|
| `DrawHeart` | `drawHeartWithNumber()` | MQTT reception, publish, setup, counter reset |
| `DrawSplash` | `drawSplashScreen()` | SoftAP setup: full-screen WIFI QR (`T:WPA`) for phone camera join |
| `DrawPowerOff` | `drawPowerOffScreen()` | Controlled shutdown: centered red `Chaya2MQTT` title |

### API for other tasks

| Function | Blocking | Timeout |
|----------|----------|---------|
| `requestHeartRedraw()` | Yes (100 ms queue) | For main/button |
| `requestHeartRedrawNonBlocking()` | No (0 ms) | For MQTT callback |
| `requestDeferredDrawSplashScreen()` | Yes (100 ms) | Setup |
| `requestDeferredDrawHeartScreen()` | Yes (100 ms) | Setup |
| `displayDrawPowerOffAndWait()` | Yes (dedicated completion semaphore) | PWR shutdown |

## Counter delta vs. absolute value

The display shows **deltas**, while MQTT transports **absolute** values:

```
Displayed RX = max(0, heartCounter − counterBaseline), capped at 999
Displayed TX = max(0, heartSentCounter − sentCountBaseline), capped at 999
```

| Variable | MQTT | Display | NVS |
|----------|------|---------|-----|
| `heartCounter` | Absolute (received) | Delta via baseline | `chaya/counter` |
| `heartSentCounter` | Absolute (sent) | Delta via baseline | `chaya/sentCount` |
| `counterBaseline` | – | RX baseline | `chaya/baseBlob` (legacy `cntBase`) |
| `sentCountBaseline` | – | TX baseline | `chaya/baseBlob` (legacy `sntBase`) |

### Example

| Action | heartCounter | counterBaseline | Displayed RX |
|--------|--------------|-----------------|--------------|
| Start | 0 | 0 | 0 |
| Partner sends 42 | 42 | 0 | 42 |
| Periodic reset (day 7) | 42 | 42 | 0 |
| Partner sends 50 | 50 | 42 | 8 |

### Overflow (≥999)

When a displayed delta reaches ≥ **999** (`kDisplayCounterMax` in `display/display_config.h`):
- The display shows `"999+"` when the delta is **greater than** 999 (exactly 999 is shown as `999`; the app can then advance the baseline)
- `maybeResetDisplayBaselinesWhenCapped()` sets the baseline to the current raw value
- The display returns to 0

## Heart geometry

Constants in `display/draw.cpp`:

| Parameter | Value | Description |
|-----------|-------|-------------|
| `kCenterX` | 100 | X coordinate of the heart's center |
| `kHeartSize` | 70 | Heart size |
| `kCircleRadius` | 43 | `(kHeartSize / 2) + 8` — radius of the two heart circles |
| `kCircleSpacing` | 32 | `(kHeartSize / 2) - 3` — distance between the circles and the center |
| `kCircleY` | 50 | Y position of the circles |
| `kTriangleTop` | 65 | `kCircleY + 15` — top point of the triangle |
| `kTriangleBottom` | 163 | Bottom point of the triangle |

Structure:
1. Two red circles (`GxEPD_RED`) at the top
2. A red triangle as the point of the heart
3. A red rectangle connecting them
4. Arrows (RX at bottom left ↓, TX at top right ↑) in the foreground color
5. Counters at the bottom (RX on the left, TX on the right) in the foreground color
6. Three small **yellow** dots to the right of the heart when RX changed since the last draw (`freshRx`)
7. Battery icon at the bottom centre; **yellow** when the pack is under 20 %

Palette: white background, black counters/arrows, red heart. The splash title stays red.

## Text rendering

| Number of digits | TextSize |
|------------------|----------|
| ≤3 digits | 4 |
| ≥4 digits | 3 |

Centering uses `getTextBounds()` after dynamic `setTextSize`. For deltas **> 999**, `"999+"` is displayed.

Footer position: Y=167, RX on the left (margin 4 + arrow lane 26), TX on the right (symmetrical).

## Refresh sequence

```mermaid
sequenceDiagram
    participant T as Display-Task
    participant S as SPI
    participant E as EPD

    T->>S: displayResumeSpiForDraw (init after hibernate, no rail cycle)
    T->>E: firstPage
    loop nextPage
        T->>E: Draw (heart, arrows, text)
        T->>E: nextPage (full refresh ~20s)
    end
    T->>E: hibernate
    T->>S: displaySuspendSpiLowPower
```

1. `displayResumeSpiForDraw` — after `hibernate()`, GxEPD2 `init(0, true, 2, false)` (RST only; rail and SPI stay up)
2. `setFullWindow()` → `firstPage()` → draw → `nextPage()` (full refresh ~20 s; fast ~15 s)
3. `hibernate()` — controller deep sleep; the image remains bistable
4. `displaySuspendSpiLowPower` — marks hibernate so the next draw re-inits

## Splash display

| Screen | Content | TextSize |
|--------|---------|----------|
| `drawSplashScreen()` | SoftAP: red „Chaya2MQTT" above a bottom-aligned WIFI QR | Title 3 (min 1) |

## Panel power / SPI

Follows Waveshare [`08_E_paper_test`](https://github.com/waveshareteam/ESP32-S3-ePaper-1.54G/tree/main/Example/Arduino_3.2.0/examples/08_E_paper_test) and GxEPD2:
- `EPD3V3_EN` (GPIO6) LOW once at boot (`digitalWrite(EPD_PWR, LOW)`); no rail cycle between frames
- Custom SPI pins (`SCK=12`, `MOSI=13`, `CS=11`) attached **before** `init()` so ESP32-S3 defaults do not swap CS/SDI
- SPI stays attached between draws; `hibernate()` wakes via RST, not `SPI.end()`
- Reset: RST HIGH 200 ms, LOW 2 ms, HIGH 200 ms (`EPD_1IN54G_Reset`)
- Busy: 100 ms then wait until BUSY is HIGH (`EPD_1IN54G_ReadBusyH`)
- First frame is painted in `setup()` **before** SoftAP RF comes up

## EPD driver

Onboard Waveshare 1.54G panel. See [HARDWARE.md](HARDWARE.md).

[GxEPD2](https://github.com/ZinggJM/GxEPD2) (`ZinggJM/GxEPD2`):
- 200 × 200, black / white / **red** / yellow
- `GxEPD2_4C` / `GxEPD2_154c_GDEM0154F51H` paging (`firstPage()` / `nextPage()`)
- Alias: `ChayaEpdPanel` in `src/display/internal.h`
- Full-window refresh (~20 s); fast mode ~15 s
- Enable panel power on GPIO6 **LOW** before drawing (active-low `EPD3V3_EN`)
- Colors: `GxEPD_BLACK`, `GxEPD_WHITE`, `GxEPD_RED`, `GxEPD_YELLOW` — the heart uses red; yellow is reserved for fresh-RX dots and a low-battery icon

## Further documentation

- Counter logic: [heart/counter](../src/heart/counter.h) → [CONFIGURATION.md](CONFIGURATION.md)
- Architecture (display task): [ARCHITECTURE.md](ARCHITECTURE.md)
