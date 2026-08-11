#!/usr/bin/env python3
"""Generate the production-intent chaya2mqtt-s2 Rev A2 schematic.

KiCad 10 exposes no schematic IPC API.  This generator writes a native KiCad
schematic, which is then normalized and checked with kicad-cli.
"""

from __future__ import annotations

import itertools
import uuid
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "chaya2mqtt-s2" / "chaya2mqtt-s2.kicad_sch"
LIB_OUT = OUT.with_suffix(".kicad_sym")
SYM_TABLE = OUT.parent / "sym-lib-table"
LIB_NAME = "chaya2mqtt_s2"
PITCH = 2.54
WIDTH = 20.32
UUID_NAMESPACE = uuid.UUID("a2c6fa51-ea4b-4f75-9fc9-61374cc677ac")
UUID_COUNTER = itertools.count()


def uid() -> str:
    return str(uuid.uuid5(UUID_NAMESPACE, str(next(UUID_COUNTER))))


@dataclass(frozen=True)
class Pin:
    number: str
    name: str
    net: str | None


@dataclass(frozen=True)
class Part:
    ref: str
    value: str
    footprint: str
    x: float
    y: float
    left: tuple[Pin, ...]
    right: tuple[Pin, ...]
    datasheet: str = ""
    manufacturer: str = ""
    mpn: str = ""


def p(number: str | int, name: str, net: str | None) -> Pin:
    return Pin(str(number), name, net)


def two(ref: str, value: str, footprint: str, x: float, y: float,
        net1: str, net2: str, datasheet: str = "", manufacturer: str = "",
        mpn: str = "") -> Part:
    return Part(ref, value, footprint, x, y, (p(1, "1", net1),),
                (p(2, "2", net2),), datasheet, manufacturer, mpn)


def aligned_y(base: float, side_count: int) -> float:
    offset = PITCH / 2 if side_count % 2 == 0 else 0.0
    return round((base - offset) / PITCH) * PITCH + offset


def symbol_name(ref: str) -> str:
    return "CHAYA_" + ref.replace("#", "PWR_").replace("-", "_")


def symbol_def(
    part: Part, *, qualified: bool
) -> tuple[str, dict[str, tuple[float, float]]]:
    n = max(len(part.left), len(part.right), 1)
    height = (n + 1) * PITCH
    pin_map: dict[str, tuple[float, float]] = {}

    def side_pins(pins: tuple[Pin, ...], side: str) -> str:
        result = []
        y0 = (len(pins) - 1) * PITCH / 2
        for index, pin in enumerate(pins):
            sy = y0 - index * PITCH
            sx = -WIDTH / 2 if side == "left" else WIDTH / 2
            rotation = 0 if side == "left" else 180
            pin_map[pin.number] = (sx, sy)
            result.append(
                f"""
        (pin passive line
          (at {sx:.3f} {sy:.3f} {rotation})
          (length 2.54)
          (name "{pin.name}" (effects (font (size 1.0 1.0))))
          (number "{pin.number}" (effects (font (size 1.0 1.0))))
        )"""
            )
        return "".join(result)

    base_name = symbol_name(part.ref)
    name = f"{LIB_NAME}:{base_name}" if qualified else base_name
    definition = f"""
    (symbol "{name}"
      (pin_names (offset 1.016))
      (exclude_from_sim no)
      (in_bom yes)
      (on_board yes)
      (property "Reference" "{part.ref.rstrip('0123456789') or part.ref}"
        (at 0 {height / 2 + 1.27:.3f} 0)
        (effects (font (size 1.27 1.27))))
      (property "Value" "{part.value}" (at 0 {-height / 2 - 1.27:.3f} 0)
        (effects (font (size 1.27 1.27))))
      (property "Footprint" "{part.footprint}" (at 0 0 0)
        (effects (font (size 1.27 1.27)) hide))
      (property "Datasheet" "{part.datasheet}" (at 0 0 0)
        (effects (font (size 1.27 1.27)) hide))
      (symbol "{base_name}_0_1"
        (rectangle (start {-WIDTH / 2:.3f} {-height / 2:.3f})
          (end {WIDTH / 2:.3f} {height / 2:.3f})
          (stroke (width 0.254) (type default))
          (fill (type background))))
      (symbol "{base_name}_1_1"
{side_pins(part.left, "left")}
{side_pins(part.right, "right")}
      )
    )"""
    return definition, pin_map


def instance(part: Part) -> str:
    n = max(len(part.left), len(part.right), 1)
    height = (n + 1) * PITCH
    extra = ""
    for key, value in (("Manufacturer", part.manufacturer), ("MPN", part.mpn)):
        if value:
            extra += f"""
    (property "{key}" "{value}" (at {part.x:.3f} {part.y:.3f} 0)
      (effects (font (size 1.27 1.27)) hide))"""
    return f"""
  (symbol (lib_id "{LIB_NAME}:{symbol_name(part.ref)}")
    (at {part.x:.3f} {part.y:.3f} 0)
    (unit 1)
    (exclude_from_sim no)
    (in_bom yes)
    (on_board yes)
    (dnp no)
    (uuid {uid()})
    (property "Reference" "{part.ref}"
      (at {part.x:.3f} {part.y - height / 2 - 1.27:.3f} 0)
      (effects (font (size 1.27 1.27))))
    (property "Value" "{part.value}"
      (at {part.x:.3f} {part.y + height / 2 + 1.27:.3f} 0)
      (effects (font (size 1.27 1.27))))
    (property "Footprint" "{part.footprint}" (at {part.x:.3f} {part.y:.3f} 0)
      (effects (font (size 1.27 1.27)) hide))
    (property "Datasheet" "{part.datasheet}" (at {part.x:.3f} {part.y:.3f} 0)
      (effects (font (size 1.27 1.27)) hide)){extra}
  )"""


def wire(x1: float, y1: float, x2: float, y2: float) -> str:
    return f"""
  (wire (pts (xy {x1:.3f} {y1:.3f}) (xy {x2:.3f} {y2:.3f}))
    (stroke (width 0) (type default))
    (uuid {uid()}))"""


def label(name: str, x: float, y: float, rotation: int) -> str:
    justify = "right" if rotation == 180 else "left"
    return f"""
  (label "{name}" (at {x:.3f} {y:.3f} {rotation})
    (effects (font (size 1.27 1.27)) (justify {justify} bottom))
    (uuid {uid()}))"""


def no_connect(x: float, y: float) -> str:
    return f"""
  (no_connect (at {x:.3f} {y:.3f}) (uuid {uid()}))"""


def note(value: str, x: float, y: float, size: float = 1.5) -> str:
    value = value.replace("\\", "\\\\").replace('"', '\\"')
    return f"""
  (text "{value}" (exclude_from_sim no) (at {x:.3f} {y:.3f} 0)
    (effects (font (size {size:.2f} {size:.2f})) (justify left top))
    (uuid {uid()}))"""


R0402 = "Resistor_SMD:R_0402_1005Metric"
R0603 = "Resistor_SMD:R_0603_1608Metric"
C0402 = "Capacitor_SMD:C_0402_1005Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
C0805 = "Capacitor_SMD:C_0805_2012Metric"
SWITCH = "Button_Switch_SMD:SW_Push_1P1T_NO_CK_KMR2"


def build_parts() -> list[Part]:
    parts: list[Part] = []

    usb_left = (
        p("A1", "GND", "GND"), p("A4", "VBUS", "VBUS"),
        p("A5", "CC1", "CC1"), p("A6", "D+", "USB_DP_C"),
        p("A7", "D-", "USB_DN_C"), p("A8", "SBU1", None),
        p("A9", "VBUS", "VBUS"), p("A12", "GND", "GND"),
        p("SH", "SHIELD", "GND"),
    )
    usb_right = (
        p("B1", "GND", "GND"), p("B4", "VBUS", "VBUS"),
        p("B5", "CC2", "CC2"), p("B6", "D+", "USB_DP_C"),
        p("B7", "D-", "USB_DN_C"), p("B8", "SBU2", None),
        p("B9", "VBUS", "VBUS"), p("B12", "GND", "GND"),
    )
    parts.append(Part(
        "J1", "HRO TYPE-C-31-M-12",
        "Connector_USB:USB_C_Receptacle_HRO_TYPE-C-31-M-12",
        35.56, aligned_y(55.88, 9), usb_left, usb_right,
        "http://www.krhro.com/uploads/soft/180320/1-1P320120243.pdf",
        "HRO", "TYPE-C-31-M-12",
    ))
    parts += [
        two("R1", "5.1k 1%", R0402, 68.58, 40.64, "CC1", "GND"),
        two("R2", "5.1k 1%", R0402, 68.58, 50.80, "CC2", "GND"),
        two("F1", "PTC 0.5A", "Fuse:Fuse_1206_3216Metric", 68.58, 60.96,
            "VBUS", "+5V"),
    ]
    parts.append(Part(
        "U2", "USBLC6-2SC6", "Package_TO_SOT_SMD:SOT-23-6",
        91.44, aligned_y(45.72, 3),
        (p(1, "I/O1", "USB_DP_C"), p(2, "GND", "GND"),
         p(3, "I/O2", "USB_DN_C")),
        (p(6, "I/O1", "USB_DP_E"), p(5, "VBUS", "VBUS"),
         p(4, "I/O2", "USB_DN_E")),
        "https://www.st.com/resource/en/datasheet/usblc6-2.pdf",
        "STMicroelectronics", "USBLC6-2SC6",
    ))
    parts += [
        two("R3", "22R", R0402, 114.30, 40.64, "USB_DN_E", "USB_DN"),
        two("R4", "22R", R0402, 114.30, 50.80, "USB_DP_E", "USB_DP"),
    ]
    parts.append(Part(
        "U3", "TLV75733PDBVR 1A", "Package_TO_SOT_SMD:SOT-23-5",
        91.44, aligned_y(73.66, 3),
        (p(1, "IN", "+5V"), p(2, "GND", "GND"), p(3, "EN", "+5V")),
        (p(5, "OUT", "+3V3"), p(4, "NC", None)),
        "https://www.ti.com/lit/ds/symlink/tlv757p.pdf",
        "Texas Instruments", "TLV75733PDBVR",
    ))
    parts += [
        two("C1", "10uF 10V X7R", C0805, 68.58, 73.66, "+5V", "GND"),
        two("C2", "100nF 10V X7R", C0402, 68.58, 83.82, "+5V", "GND"),
        two("C3", "10uF 10V X7R", C0805, 114.30, 73.66, "+3V3", "GND"),
        two("C4", "100nF 10V X7R", C0402, 114.30, 83.82, "+3V3", "GND"),
    ]

    gnd_pins = {1, 2, 30, 42, 43, *range(46, 66)}
    used = {
        3: ("3V3", "+3V3"), 4: ("IO0/BOOT", "BOOT"),
        8: ("IO4/BTN", "BTN"), 9: ("IO5/LED", "LED_GATE"),
        10: ("IO6/BUSY", "EPD_BUSY"), 11: ("IO7/RST", "EPD_RST"),
        14: ("IO10/CS", "EPD_CS"), 15: ("IO11/MOSI", "EPD_MOSI"),
        16: ("IO12/SCK", "EPD_SCK"), 19: ("IO15/DC", "EPD_DC"),
        23: ("IO19/USB_D-", "USB_DN"), 24: ("IO20/USB_D+", "USB_DP"),
        45: ("EN", "EN"),
    }
    mcu_left: list[Pin] = []
    mcu_right: list[Pin] = []
    for number in range(1, 66):
        if number in gnd_pins:
            pin = p(number, "GND", "GND")
        elif number in used:
            pin = p(number, used[number][0], used[number][1])
        else:
            pin = p(number, "NC/UNUSED", None)
        (mcu_left if number <= 33 else mcu_right).append(pin)
    parts.append(Part(
        "U1", "ESP32-S2-MINI-2-N4R2", "RF_Module:ESP32-S2-MINI-1",
        182.88, aligned_y(83.82, max(len(mcu_left), len(mcu_right))),
        tuple(mcu_left), tuple(mcu_right),
        "https://documentation.espressif.com/esp32-s2-mini-2_esp32-s2-mini-2u_datasheet_en.pdf",
        "Espressif", "ESP32-S2-MINI-2-N4R2",
    ))
    parts += [
        two("C5", "22uF 10V X7R", C0805, 147.32, 132.08, "+3V3", "GND"),
        two("C6", "100nF 10V X7R", C0402, 170.18, 132.08, "+3V3", "GND"),
        two("R5", "10k", R0402, 147.32, 142.24, "+3V3", "EN"),
        two("C7", "1uF 10V X7R", C0603, 170.18, 142.24, "EN", "GND"),
        two("SW1", "RESET", SWITCH, 193.04, 142.24, "EN", "GND"),
        two("R6", "10k", R0402, 147.32, 152.40, "+3V3", "BOOT"),
        two("SW2", "BOOT", SWITCH, 170.18, 152.40, "BOOT", "GND"),
    ]

    panel_left = (
            p(1, "NC", None), p(2, "GDR", "GDR"), p(3, "RESE", "RESE"),
            p(4, "NC", None), p(5, "VSH2/VDHR", "VSH2"),
            p(6, "TSCL", None), p(7, "TSDA", None), p(8, "BS", "GND"),
            p(9, "BUSY_N", "EPD_BUSY"), p(10, "RST_N", "EPD_RST"),
            p(11, "DC", "EPD_DC"), p(12, "CSB", "EPD_CS"),
    )
    panel_right = (
            p(13, "SCL", "EPD_SCK"), p(14, "SDA", "EPD_MOSI"),
            p(15, "VDDIO", "+3V3"), p(16, "VCI/VDD", "+3V3"),
            p(17, "VSS", "GND"), p(18, "VDDD", "VDD"),
            p(19, "VPP", None), p(20, "VSH1", "VSH1"),
            p(21, "VGH", "PREVGH"), p(22, "VSL", "VSL"),
            p(23, "VGL", "PREVGL"), p(24, "VCOM", "VCOM"),
    )
    parts.append(Part(
        "J2", "GDEM0154F61H FPC 24P 0.5mm",
        "Connector_FFC-FPC:Hirose_FH12-24S-0.5SH_1x24-1MP_P0.50mm_Horizontal",
        327.66, aligned_y(73.66, 12), panel_left, panel_right,
        "https://www.good-display.com/companyfile/2085.html",
        "Good Display", "GDEM0154F61H",
    ))

    parts += [
        two("L1", "47uH 500mA NR3015",
            "Inductor_SMD:L_Taiyo-Yuden_NR-30xx", 248.92, 43.18,
            "+3V3", "SW"),
        Part("Q2", "Si1308EDL", "Package_TO_SOT_SMD:SOT-323_SC-70",
             271.78, aligned_y(50.80, 2),
             (p(1, "G", "GDR"), p(2, "S", "RESE")),
             (p(3, "D", "SW"),),
             "https://www.vishay.com/docs/63399/si1308edl.pdf",
             "Vishay", "SI1308EDL-T1-GE3"),
        two("R10", "1M 1%", R0402, 248.92, 55.88, "GDR", "GND"),
        two("R11", "2.2R 1%", R0603, 294.64, 55.88, "RESE", "GND"),
        two("D1", "MBR0530", "Diode_SMD:D_SOD-123", 248.92, 68.58,
            "FLY", "PREVGL"),
        two("D2", "MBR0530", "Diode_SMD:D_SOD-123", 271.78, 68.58,
            "GND", "FLY"),
        two("D3", "MBR0530", "Diode_SMD:D_SOD-123", 294.64, 68.58,
            "PREVGH", "SW"),
        two("C8", "4.7uF 25V X7R", C0805, 248.92, 81.28, "+3V3", "GND"),
        two("C9", "4.7uF 25V X7R", C0805, 271.78, 81.28, "SW", "FLY"),
        two("C10", "1uF 25V X7R", C0603, 294.64, 81.28, "PREVGH", "GND"),
        two("C11", "1uF 25V X7R", C0603, 248.92, 93.98, "VSH2", "GND"),
        two("C12", "1uF 25V X7R", C0603, 271.78, 93.98, "+3V3", "GND"),
        two("C13", "1uF 25V X7R", C0603, 294.64, 93.98, "VDD", "GND"),
        two("C14", "1uF 25V X7R", C0603, 248.92, 106.68, "VSH1", "GND"),
        two("C15", "1uF 25V X7R", C0603, 271.78, 106.68, "VSL", "GND"),
        two("C16", "1uF 25V X7R", C0603, 294.64, 106.68,
            "PREVGH", "PREVGL"),
        two("C17", "1uF 25V X7R", C0603, 317.50, 106.68, "VCOM", "GND"),
    ]

    parts.append(Part(
        "J3", "LED-Ring-Taster",
        "Connector_JST:JST_SH_SM04B-SRSS-TB_1x04-1MP_P1.00mm_Horizontal",
        50.80, aligned_y(180.34, 2),
        (p(1, "BTN", "BTN"), p(2, "GND", "GND")),
        (p(3, "LED_A", "+3V3"), p(4, "LED_K", "LED_K")),
        "", "", "",
    ))
    parts += [
        two("R7", "10k", R0402, 83.82, 170.18, "+3V3", "BTN"),
        two("R8", "100R", R0402, 83.82, 180.34, "LED_GATE", "LED_DRV"),
        two("R9", "100k", R0402, 83.82, 190.50, "LED_DRV", "GND"),
        Part("Q1", "2N7002", "Package_TO_SOT_SMD:SOT-23",
             111.76, aligned_y(180.34, 2),
             (p(1, "G", "LED_DRV"),),
             (p(2, "S", "GND"), p(3, "D", "LED_K"))),
    ]

    for index, (net, x) in enumerate(
        [("+3V3", 147.32), ("GND", 170.18), ("USB_DP", 193.04),
         ("USB_DN", 215.90), ("EPD_BUSY", 238.76)],
        start=1,
    ):
        parts.append(Part(
            f"TP{index}", net, "TestPoint:TestPoint_Pad_D1.0mm",
            x, aligned_y(190.50, 1), (p(1, net, net),), (),
        ))
    return parts


def main() -> None:
    parts = build_parts()
    definitions: list[str] = []
    library_definitions: list[str] = []
    pin_maps: dict[str, dict[str, tuple[float, float]]] = {}
    for part in parts:
        definition, pin_map = symbol_def(part, qualified=True)
        library_definition, _ = symbol_def(part, qualified=False)
        definitions.append(definition)
        library_definitions.append(library_definition)
        pin_maps[part.ref] = pin_map

    body: list[str] = []
    for part in parts:
        body.append(instance(part))
        for side, pins in (("left", part.left), ("right", part.right)):
            for pin in pins:
                sx, sy = pin_maps[part.ref][pin.number]
                x = part.x + sx
                y = part.y - sy
                if pin.net is None:
                    body.append(no_connect(x, y))
                    continue
                dx = -5.08 if side == "left" else 5.08
                body.append(wire(x, y, x + dx, y))
                body.append(label(pin.net, x + dx, y, 180 if side == "left" else 0))

    body += [
        note("USB-C DEVICE + ESD + 3V3 POWER", 15.24, 22.86, 1.8),
        note("ESP32-S2-MINI-2-N4R2 / NATIVE USB", 137.16, 22.86, 1.8),
        note("GDEM0154F61H / SSD2681 OFFICIAL APPLICATION", 238.76, 22.86, 1.8),
        note("WIRED RED LED-RING BUTTON", 15.24, 157.48, 1.8),
        note(
            "Rev A2: GDEM0154F61H specification Rev 1.0 (2026-05-15).\\n"
            "D1-D3 pad 1 = cathode, pad 2 = anode. BS/TSCL/TSDA tied to GND.\\n"
            "Prototype requires EPD HV measurement, USB SI and RF validation.",
            238.76, 124.46, 1.4,
        ),
    ]

    schematic = f"""(kicad_sch (version 20231120) (generator "chaya2mqtt")
  (uuid {uid()})
  (paper "A2")
  (title_block
    (title "chaya2mqtt-s2 — ESP32-S2-MINI-2 + GDEM0154F61H")
    (date "2026-08-10")
    (rev "A2")
    (company "chaya2mqtt")
    (comment 1 "46 x 45 mm four-layer prototype")
    (comment 2 "USB-C rear / E-Paper front / wired LED-ring button")
    (comment 3 "Official panel boost values; electrical bring-up required"))
  (lib_symbols
{''.join(definitions)}
  )
{''.join(body)}
  (sheet_instances
    (path "/" (page "1")))
)
"""
    OUT.write_text(schematic)
    LIB_OUT.write_text(
        "(kicad_symbol_lib (version 20231120) (generator chaya2mqtt)\n"
        + "".join(library_definitions)
        + "\n)\n"
    )
    SYM_TABLE.write_text(
        "(sym_lib_table\n"
        "  (version 7)\n"
        '  (lib (name "chaya2mqtt")(type "KiCad")'
        '(uri "${KIPRJMOD}/../lib/chaya2mqtt.kicad_sym")'
        '(options "")(descr "Shared chaya2mqtt custom symbols"))\n'
        f'  (lib (name "{LIB_NAME}")(type "KiCad")'
        f'(uri "${{KIPRJMOD}}/{LIB_OUT.name}")'
        '(options "")(descr "Generated Rev A2 symbols"))\n'
        ")\n"
    )
    print(f"Wrote {OUT} and {LIB_OUT} ({len(parts)} parts)")


if __name__ == "__main__":
    main()
