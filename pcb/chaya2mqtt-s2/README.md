# chaya2mqtt-s2 Rev A2

Kompaktes vierlagiges 46,0 × 45,0-mm-Board für ESP32-S2-MINI-2-N4R2 und
GDEM0154F61H. Das E-Paper wird auf der Vorderseite im Gehäuse befestigt, USB-C
sitzt rückseitig an der unteren Kante und der beleuchtete Taster wird über J3
verkabelt.

## Zielhardware

- ESP32-S2-MINI-2-N4R2: WiFi, native USB, kein Bluetooth, 4 MB Flash, 2 MB PSRAM
- GDEM0154F61H: 1,54 Zoll, 200×200, Schwarz/Weiß/Rot/Gelb, SSD2681
- USB-C hinten; Display vorne; kabelgebundener roter LED-Ring-Taster
- Vier Lagen: F.Cu / In1.Cu GND / In2.Cu 3V3 / B.Cu

## Elektrische Blöcke

- USB-C mit 5,1-kΩ-CC-Widerständen, ESD, PTC und 22-Ω-Serienwiderständen
- TLV75733PDBVR (1 A) mit Ein-/Ausgangsentkopplung
- EN/BOOT, Reset-/Boot-Taster und fünf Testpunkte
- 24-poliger 0,5-mm-FPC für das Rohpanel
- SSD2681-Boost gemäß GDEM0154F61H Rev 1.0 vom 2026-05-15

## Status

- Der Schaltplan ist elektrisch vollständig; KiCad 10 ERC meldet 0 Verstöße.
- Offene MCU-/Panel-Pins sind explizit als `No Connect` markiert.
- Das 46 × 45-mm-Layout ist vollständig geroutet; KiCad 10 DRC meldet
  0 Verstöße und 0 offene Verbindungen.
- In1 enthält eine GND-Fläche, In2 eine 3V3-Fläche. Beide beginnen unterhalb
  des ESP32-Antennen-Keepouts.
- KiCad-Quellen, BOM, CPL und Fertigungsexporte liegen unter `production/`.
- Zwei M2-Bohrungen unten; oben bleibt wegen des offiziellen Antennen-Keepouts
  bohrungsfrei.
- Das Board ist ein Prototyp. Vor einer Serienbestellung sind elektrischer
  Bring-up, E-Paper-Hochspannungsmessung, USB-Signalintegrität, mechanische
  FPC-/Gehäuseprüfung und ein 2,4-GHz-RF-Test erforderlich.

`../tools/generate_chaya2mqtt_s2_schematic.py` erzeugt den Schaltplan
reproduzierbar. `../tools/apply_schematic_netlist.py` überträgt die XML-Netzliste
einschließlich mehrfach vorhandener Footprint-Pads eindeutig auf das PCB.
Fertigungsdaten, PDF, BOM, ERC und DRC werden mit `kicad-cli` exportiert.
