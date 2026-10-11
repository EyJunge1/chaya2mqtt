//! Battery ADC + soft-off sleep.

use crate::hw::pins;
use crate::util::platform::{GpioIn, GpioOut};
use chaya_pure::battery::{battery_critical_low, battery_pct_from_milli_volts};

#[derive(Clone, Debug, Default)]
pub struct BatteryService {
    pub last_pct: i32,
    pub last_mv: i32,
}

impl BatteryService {
    /// `adc_mv` is already pack millivolts (after ×2 divider compensation).
    pub fn update_from_pack_mv(&mut self, adc_mv: i32) -> i32 {
        self.last_mv = adc_mv;
        self.last_pct = battery_pct_from_milli_volts(adc_mv);
        self.last_pct
    }

    pub fn critical(&self) -> bool {
        battery_critical_low(self.last_pct)
    }

    pub fn pwr_pressed(&self, gpio: &mut dyn GpioIn) -> bool {
        !gpio.get_input(pins::PWR_BUTTON)
    }
}

/// Cut latch and enter deep sleep (device HAL later).
pub fn power_off_and_sleep(gpio: &mut dyn GpioOut) {
    gpio.set_output(pins::BAT_CONTROL, false);
}
