# ERC classifications

KiCad 10 ERC reports **0 violations** for Rev A2. All electrical pins are
either assigned to a named net or explicitly marked as `No Connect`.
The generated custom symbols are stored in `chaya2mqtt-s2.kicad_sym`,
registered in the local `sym-lib-table`, and additionally embedded in the
schematic file.

No project-specific ERC exclusions are required for this schematic.

Passing ERC does not constitute production approval. E-paper high voltages,
USB, FPC orientation, LDO thermal behavior, and RF behavior must be tested on
the prototype.
