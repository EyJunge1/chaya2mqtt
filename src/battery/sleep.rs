//! Soft-off: cut battery latch and enter deep sleep with EXT1 wake on PWR.

use esp_hal::gpio::{Event, Input, Level, Output, WakeupConfig};
use esp_hal::peripherals::LPWR;
use esp_hal::rtc_cntl::sleep::{LowPower, RtcSleepConfig};
use esp_println::println;

/// Arm PWR (active-low) as deep-sleep wake, cut latch, sleep. Does not return on success.
pub fn power_off_deep_sleep(pwr: &mut Input<'static>, bat_latch: &mut Output<'static>) -> ! {
    // Hold pad high while released; wake on low (button press).
    pwr.listen(Event::LowLevel);
    if let Err(e) = pwr.apply_wakeup_config(&WakeupConfig::default().with_low_power_path(true)) {
        println!("PWR: wakeup config failed: {e:?} — latch cut only");
        bat_latch.set_level(Level::Low);
        loop {
            // Fall back: wait forever with latch cut (external PWR cycle).
            core::hint::spin_loop();
        }
    }

    // Wait until PWR is released (high) so EXT1 ANY_LOW is not already asserted.
    for _ in 0..200 {
        if pwr.is_high() {
            break;
        }
        // Busy short wait — no Embassy timer once we're shutting down.
        for _ in 0..50_000 {
            core::hint::spin_loop();
        }
    }

    bat_latch.set_level(Level::Low);
    println!("PWR: deep sleep (wake=PWR LOW)");

    if pwr.is_low() {
        println!("PWR: still LOW — cannot arm EXT1 safely");
        loop {
            core::hint::spin_loop();
        }
    }

    let mut lp = LowPower::new(unsafe { LPWR::steal() });
    lp.sleep_deep(RtcSleepConfig::deep());
}
