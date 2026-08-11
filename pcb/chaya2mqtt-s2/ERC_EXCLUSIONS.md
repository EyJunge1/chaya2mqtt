# ERC-Einstufungen

KiCad 10 ERC meldet für Rev A2 **0 Verstöße**. Alle elektrischen Pins sind
entweder einem benannten Netz zugeordnet oder explizit als `No Connect`
markiert. Die erzeugten Custom-Symbole liegen in
`chaya2mqtt-s2.kicad_sym`, sind im lokalen `sym-lib-table` registriert und
zusätzlich in der Schaltplandatei eingebettet.

Für diesen Schaltplan sind keine projektspezifischen ERC-Ausnahmen erforderlich.

ERC-Freiheit ist keine Produktionsfreigabe. E-Paper-Hochspannungen, USB,
FPC-Ausrichtung, LDO-Thermik und RF-Verhalten müssen am Prototyp geprüft werden.
