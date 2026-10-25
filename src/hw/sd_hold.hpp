#pragma once

/** Drive TF/microSD GPIOs LOW; no SDMMC driver, no FAT mount. Call early in setup(). */
void sdHoldOff();
