#pragma once

/**
 * Permanently disable unused e-Paper touch (1.54G has no touch panel, ever)
 * and park RTC INT. Call early in setup() with sdHoldOff().
 */
void touchHoldOff();
