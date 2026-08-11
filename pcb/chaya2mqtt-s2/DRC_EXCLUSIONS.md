# DRC-Einstufungen

KiCad 10 meldet für Rev A2 **0 Verstöße und 0 offene Verbindungen**.

Die globale Prüfung `lib_footprint_mismatch` ist deaktiviert. U1 und J1 sind
als vollständige Footprints im PCB eingebettet; ihre geprüfte und exportierte
Geometrie ist damit unabhängig von späteren Änderungen der installierten
KiCad-Standardbibliothek. Alle Fertigungs-, Abstands-, Kurzschluss-,
Antennen-Keepout- und Verbindungsprüfungen bleiben aktiv.

DRC-Freiheit ersetzt nicht den elektrischen, thermischen, mechanischen und
HF-Bring-up des ersten Prototyps.
