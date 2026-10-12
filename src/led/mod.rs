//! Header LED patterns (active-low on board).

use crate::hw::pins;
use crate::util::platform::GpioOut;
use crate::core::led::{led_pattern_advance, led_pattern_begin, LedPatternRuntime};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum LedPreset {
    Boot,
    WifiUp,
    MqttUp,
    LinkDown,
    SoftOff,
    /// Short hold/pulse while MQTT publish awaits PUBACK (C++ send-sequence reserve).
    TxPending,
    /// PUBACK success ack (C++ post-publish blink).
    TxAck,
    /// PUBACK timeout / publish fail (C++ fail flash ≈ LinkDown).
    TxFail,
}

impl LedPreset {
    pub(crate) fn params(self) -> (u8, u16, u16) {
        match self {
            Self::Boot => (3, 200, 200),
            Self::WifiUp => (2, 80, 80),
            Self::MqttUp => (1, 150, 0),
            Self::LinkDown => (4, 50, 50),
            Self::SoftOff => (3, 60, 60),
            // Two short pulses ≈ C++ PreOn1/PreOn2 reserve while publish is in flight.
            Self::TxPending => (2, 100, 100),
            Self::TxAck => (1, 40, 40),
            // Fail flash ≈ LinkDown / SoftOff-ish.
            Self::TxFail => (4, 50, 50),
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

    /// Play preset only when LED is enabled; otherwise force off and clear activity.
    pub fn play_preset_gated(&mut self, preset: LedPreset, enabled: bool, gpio: &mut dyn GpioOut) {
        if !enabled {
            self.force_off(gpio);
            return;
        }
        self.play_preset(preset);
    }

    pub fn force_off(&mut self, gpio: &mut dyn GpioOut) {
        self.active = false;
        // Active-low LED off = pin high.
        gpio.set_output(pins::BUTTON_LED, true);
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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn tx_presets_distinct() {
        assert_ne!(LedPreset::TxPending.params(), LedPreset::TxAck.params());
        assert_eq!(LedPreset::TxFail.params(), LedPreset::LinkDown.params());
        assert_eq!(LedPreset::TxPending.params(), (2, 100, 100));
        assert_eq!(LedPreset::TxAck.params(), (1, 40, 40));
    }

    #[test]
    fn play_preset_gated_respects_enabled() {
        struct Pin {
            high: bool,
        }
        impl GpioOut for Pin {
            fn set_output(&mut self, _pin: u8, high: bool) {
                self.high = high;
            }
        }
        let mut led = LedService::default();
        let mut gpio = Pin { high: true };
        led.play_preset_gated(LedPreset::TxPending, false, &mut gpio);
        assert!(!led.active);
        assert!(gpio.high);
        led.play_preset_gated(LedPreset::TxAck, true, &mut gpio);
        assert!(led.active);
    }
}
