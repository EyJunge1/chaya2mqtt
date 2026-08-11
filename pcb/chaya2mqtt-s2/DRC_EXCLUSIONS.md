# DRC classifications

KiCad 10 reports **0 violations and 0 unconnected items** for Rev A2.

The global `lib_footprint_mismatch` check is disabled. U1 and J1 are embedded
as complete footprints in the PCB, making their verified and exported geometry
independent of later changes to the installed KiCad standard library. All
manufacturing, clearance, short-circuit, antenna keepout, and connectivity
checks remain active.

Passing DRC does not replace the electrical, thermal, mechanical, and RF
bring-up of the first prototype.
