//! Header LED patterns (active-low on board).

use crate::board::pins;
use crate::platform::GpioOut;
use chaya_pure::led::{led_pattern_advance, led_pattern_begin, LedPatternRuntime};

pub struct LedService {
    pub rt: LedPatternRuntime,
    pub active: bool,
}

impl Default for LedService {
    fn default() -> Self {
        Self {
            rt: LedPatternRuntime::default(),
            active: false,
        }
    }
}

impl LedService {
    pub fn start_blink(&mut self, count: u8, on_ms: u16, off_ms: u16) {
        led_pattern_begin(&mut self.rt, count, on_ms, off_ms);
        self.active = true;
    }

    pub fn startup_blink(&mut self) {
        self.start_blink(2, 80, 80);
    }

    /// Drive LED for current phase; returns `true` when pattern finished.
    pub fn tick_phase_done(&mut self, gpio: &mut dyn GpioOut) -> bool {
        if !self.active {
            return true;
        }
        let r = led_pattern_advance(&mut self.rt);
        // Active-low LED.
        gpio.set_output(pins::BUTTON_LED, !r.led_on);
        if r.done {
            self.active = false;
            gpio.set_output(pins::BUTTON_LED, true);
        }
        r.done
    }
}
