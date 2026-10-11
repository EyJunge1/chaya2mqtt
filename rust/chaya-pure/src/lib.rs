//! Host-testable business rules ported from C++ `*_pure.h`.
//! `no_std` so the same crate can later link into an esp-hal firmware binary.

#![cfg_attr(not(test), no_std)]

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
