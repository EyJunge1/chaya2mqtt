#pragma once

/**
 * Hard-disable unused SHTC3 temp/humidity (I2C 0x70): sleep command 0xB098.
 * Board has no sensor VDD load-switch (tied to 3V3) — sleep (~0.3 uA) is the
 * strongest software kill; never wake, never measure. Requires Wire begun.
 */
void shtc3Sleep();
