//! Shared runtime helpers.

pub struct MonoClock;
impl chaya_fw::storage::platform::ClockMs for MonoClock {
    fn now_ms(&self) -> u32 {
        (embassy_time::Instant::now().as_millis() & 0xffff_ffff) as u32
    }
}
