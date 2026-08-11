# PCB — Hardware-Quelle der Wahrheit

Dieser Ordner dokumentiert und spezifiziert die physische Chaya2MQTT-Hardware.

| Pfad | Inhalt |
|------|--------|
| `current-reference/` | KiCad-Referenzschaltplan der **aktuellen** COTS-Hardware (Waveshare ESP32 Driver Board + 1,54″-BWR + Taster/LED) |
| `chaya2mqtt-s2/` | Future-Schaltplan mit ESP32-S2-MINI-2 und GDEM0154F61H |
| `chaya2mqtt-s2/production/` | ERC/BOM/PDF; Layout und Fertigungsdaten warten auf Gehäusemaße |
| `lib/` | Projektweite Custom-Symbole |

Firmware-Pinmapping:
- Waveshare / Default: `src/hw/pins_esp32_waveshare.h`

Die Firmware bleibt unverändert. Der S2-Ordner ist ausschließlich Hardwareplanung.

Siehe auch `docs/HARDWARE.md`.

## Gates vor physischer Bestellung

1. Panelcode/FPC des verbauten 1,54″-BWR-Panels manuell bestätigen (Ziel: Waveshare 1.54″ e-Paper (B) / GDEH0154Z90-Familie, 24-Pin 0,5 mm).
2. EPD-Boost-Bauteilwerte gegen Datenblatt-Figur 7-5 (Waveshare B Spec) bzw. Panel-Hersteller freigeben — **elektrische Erstfreigabe Pflicht**.
3. KiCad ERC/DRC ohne ungeklärte Fehler (`scripts/pcb_erc_drc.sh`).
4. Mechanik/Gehäuse festlegen: Display vorne, USB-C hinten, roter LED-Ring-Taster oben.
5. Erst danach Layout, DRC, Gerber, Drill und Pick-and-Place erzeugen.
