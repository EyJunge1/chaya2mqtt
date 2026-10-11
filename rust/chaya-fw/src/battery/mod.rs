//! Battery service (+ ADC on device) — mirrors C++ `src/battery/`.

#[cfg(feature = "device")]
pub mod adc;
#[cfg(feature = "device")]
pub mod sleep;

mod service;
pub use service::{power_off_and_sleep, BatteryService};
