# PCB — Hardware source of truth

This directory documents and specifies the physical Chaya2MQTT hardware.

| Path | Contents |
|------|--------|
| `current-reference/` | KiCad reference schematic for the **current** COTS hardware (Waveshare ESP32 Driver Board + 1.54″ BWR + button/LED) |
| `chaya2mqtt-s2/` | Future schematic with ESP32-S2-MINI-2 and GDEM0154F61H |
| `chaya2mqtt-s2/production/` | ERC/BOM/PDF; layout and manufacturing data await enclosure dimensions |
| `lib/` | Project-wide custom symbols |

Firmware pin mapping:
- Waveshare / Default: `src/hw/pins_esp32_waveshare.h`

The firmware remains unchanged. The S2 directory is exclusively for hardware planning.

See also `docs/HARDWARE.md`.

## Gates before ordering physical hardware

1. Manually confirm the panel code/FPC of the installed 1.54″ BWR panel (target: Waveshare 1.54″ e-Paper (B) / GDEH0154Z90 family, 24-pin 0.5 mm).
2. Validate the EPD boost component values against datasheet Figure 7-5 (Waveshare B Spec) or with the panel manufacturer—**initial electrical approval is mandatory**.
3. KiCad ERC/DRC without unresolved errors (`scripts/pcb_erc_drc.sh`).
4. Finalize the mechanical design/enclosure: display at the front, USB-C at the rear, red LED ring button at the top.
5. Only then generate the layout, DRC, Gerber, drill, and pick-and-place files.
