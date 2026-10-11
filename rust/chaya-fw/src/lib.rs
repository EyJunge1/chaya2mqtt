//! Full Chaya2MQTT firmware application (esp-hal target).
//!
//! Module layout mirrors C++ `src/`. Host builds exercise orchestration + pure logic.
//! Radio / EPD / TLS / HTTP backends are trait-shaped stubs until upstream crates are wired
//! (see `docs/RUST_ESP_HAL.md`).

#![cfg_attr(not(test), no_std)]

pub mod app;
pub mod audio;
pub mod battery;
pub mod board;
pub mod button;
pub mod config;
pub mod display;
pub mod heart;
pub mod identity;
pub mod led;
pub mod mqtt;
pub mod network;
pub mod ota;
pub mod platform;
pub mod version;
pub mod web;
pub mod wifi;

#[cfg(feature = "device")]
pub mod device;

pub use app::{boot, BootReport, Firmware};
