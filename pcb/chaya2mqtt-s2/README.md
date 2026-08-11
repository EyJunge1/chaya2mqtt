# chaya2mqtt-s2 Rev A2

Compact four-layer 46.0 × 45.0 mm board for ESP32-S2-MINI-2-N4R2 and
GDEM0154F61H. The e-paper display is mounted at the front of the enclosure,
USB-C sits at the lower edge on the rear, and the illuminated button is
connected through J3.

## Target hardware

- ESP32-S2-MINI-2-N4R2: Wi-Fi, native USB, no Bluetooth, 4 MB flash, 2 MB PSRAM
- GDEM0154F61H: 1.54 inch, 200×200, black/white/red/yellow, SSD2681
- USB-C at the rear; display at the front; wired red LED ring button
- Four layers: F.Cu / In1.Cu GND / In2.Cu 3V3 / B.Cu

## Electrical blocks

- USB-C with 5.1 kΩ CC resistors, ESD protection, PTC, and 22 Ω series resistors
- TLV75733PDBVR (1 A) with input/output decoupling
- EN/BOOT, reset/boot buttons, and five test points
- 24-pin 0.5 mm FPC for the raw panel
- SSD2681 boost circuit according to GDEM0154F61H Rev 1.0 dated 2026-05-15

## Status

- The schematic is electrically complete; KiCad 10 ERC reports 0 violations.
- Unused MCU/panel pins are explicitly marked as `No Connect`.
- The 46 × 45 mm layout is fully routed; KiCad 10 DRC reports
  0 violations and 0 unconnected items.
- In1 contains a GND plane, and In2 contains a 3V3 plane. Both start below
  the ESP32 antenna keepout.
- KiCad sources, BOM, CPL, and manufacturing exports are located in `production/`.
- Two M2 holes at the bottom; the top remains free of holes due to the official
  antenna keepout.
- The board is a prototype. Electrical bring-up, e-paper high-voltage
  measurements, USB signal integrity testing, mechanical FPC/enclosure checks,
  and a 2.4 GHz RF test are required before a production order.

`../tools/generate_chaya2mqtt_s2_schematic.py` generates the schematic
reproducibly. `../tools/apply_schematic_netlist.py` maps the XML netlist
unambiguously onto the PCB, including duplicated footprint pads.
Manufacturing data, PDF, BOM, ERC, and DRC are exported with `kicad-cli`.
