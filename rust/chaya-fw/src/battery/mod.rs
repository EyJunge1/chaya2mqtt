//! Battery service (+ ADC on device) — mirrors C++ `src/battery/`.

#[cfg(feature = "device")]
pub mod adc;

mod service;
pub use service::BatteryService;
