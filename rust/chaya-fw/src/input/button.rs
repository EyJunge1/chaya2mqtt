//! BOOT button debounce + soft-off (GPIO backend via platform).

use crate::board::pins;
use crate::platform::{ClockMs, GpioIn};
use chaya_pure::button::{
    debounce_update, soft_off_may_arm_ext1_wake, soft_off_release_settled, DebouncedGpioState,
    SoftOffReleaseSettle,
};

pub const DEBOUNCE_MS: u32 = 30;
pub const SOFT_OFF_SETTLE_MS: u32 = 50;

pub struct ButtonService {
    pub boot: DebouncedGpioState,
    pub soft_off_settle: SoftOffReleaseSettle,
}

impl Default for ButtonService {
    fn default() -> Self {
        Self {
            boot: DebouncedGpioState {
                last_raw_reading: 1,
                last_debounce_change_ms: 0,
                debounced_level: 1,
            },
            soft_off_settle: SoftOffReleaseSettle::default(),
        }
    }
}

impl ButtonService {
    /// Active-low BOOT: returns true on press edge (high→low of debounced level).
    pub fn poll_boot_press(
        &mut self,
        gpio: &mut dyn GpioIn,
        clock: &dyn ClockMs,
    ) -> bool {
        let prev = self.boot.debounced_level;
        let raw = if gpio.get_input(pins::BUTTON) { 1 } else { 0 };
        debounce_update(&mut self.boot, raw, clock.now_ms(), DEBOUNCE_MS);
        prev != 0 && self.boot.debounced_level == 0
    }

    pub fn soft_off_ready(&mut self, gpio: &mut dyn GpioIn, clock: &dyn ClockMs) -> bool {
        let level = if gpio.get_input(pins::PWR_BUTTON) {
            1
        } else {
            0
        };
        if !soft_off_may_arm_ext1_wake(level) {
            return false;
        }
        soft_off_release_settled(
            &mut self.soft_off_settle,
            level,
            clock.now_ms(),
            SOFT_OFF_SETTLE_MS,
        )
    }
}
