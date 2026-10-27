#pragma once

/**
 * Hard-disable unused microSD: no SDMMC driver, no FAT mount.
 * Board has no SD VDD load-switch (slot tied to 3V3) — this is the strongest
 * software kill: CLK/DAT0/CMD held OUTPUT LOW so the bus cannot float-draw.
 * Call early in setup().
 */
void sdHoldOff();
