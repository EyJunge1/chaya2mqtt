#pragma once

/**
 * Hard-disable unused PCF85063 RTC (I2C 0x51): STOP clock, CLKOUT low, timer off.
 * No VDD load-switch on the PCB — this is the strongest software kill. Never start.
 * Requires Wire already begun.
 */
void pcf85063Stop();
