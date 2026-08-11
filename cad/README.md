# CAD — Enclosure

This directory is intended for the mechanical CAD files of the Chaya2MQTT enclosure.

The project currently supports the Waveshare COTS hardware. Enclosure models should therefore use the dimensions of this current hardware.

Once the new hardware has been finalized and validated, its enclosure models will be added here. Until then, designs for potential future PCBs must be clearly marked as preliminary.

Preferred exchange formats:

- Source files from the CAD application used in `cad/`
- `STEP` for editable 3D geometry and manufacturing in `cad/`
- `STL` or `3MF` for 3D printing in `cad/exports/`

Each enclosure design should document its variant, target hardware, revision, and required screws or print parameters.
