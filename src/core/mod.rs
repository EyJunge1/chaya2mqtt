//! Host-testable business rules (no hardware I/O).
//! Kept as `core` so unit tests run on the host without the `device` feature.

pub mod async_shutdown;
pub mod audio;
pub mod battery;
pub mod button;
pub mod config_nvs;
pub mod constants;
pub mod display;
pub mod heart;
pub mod identity;
pub mod led;
pub mod mqtt;
pub mod ota;
pub mod web;
pub mod wifi;
pub mod wifi_recovery;
