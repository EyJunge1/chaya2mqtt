//! Header LED patterns (active-low on board).

use crate::hw::pins;
use crate::util::platform::GpioOut;
use chaya_pure::led::{led_pattern_advance, led_pattern_begin, LedPatternRuntime};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum LedPreset {
    Boot,
    WifiUp,
    MqttUp,
    LinkDown,
    SoftOff,
    TxAck,
}

impl LedPreset {
    fn params(self) -> (u8, u16, u16) {
        match self {
            Self::Boot => (3, 200, 200),
            Self::WifiUp => (2, 80, 80),
            Self::MqttUp => (1, 150, 0),
            Self::LinkDown => (4, 50, 50),
            Self::SoftOff => (3, 60, 60),
            Self::TxAck => (1, 40, 40),
        }
    }
}

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

    pub fn play_preset(&mut self, preset: LedPreset) {
        let (c, on, off) = preset.params();
        self.start_blink(c, on, off);
    }

    pub fn startup_blink(&mut self) {
        self.play_preset(LedPreset::Boot);
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
