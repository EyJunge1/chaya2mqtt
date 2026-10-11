//! Full Chaya2MQTT firmware application (esp-hal target).
//!
//! Layout mirrors C++ `src/`: thematic folders; device backends gated by `--features device`.

#![cfg_attr(not(test), no_std)]

#[cfg(feature = "device")]
extern crate alloc;

pub mod app;
pub mod audio;
pub mod battery;
pub mod button;
pub mod config;
pub mod display;
pub mod heart;
pub mod hw;
pub mod identity;
pub mod led;
pub mod mqtt;
pub mod network;
pub mod ota;
#[cfg(feature = "device")]
pub mod tls;
pub mod util;
pub mod web;
pub mod wifi;

pub use app::{boot, BootReport, Firmware};
pub use config::APP_VERSION;
pub use heart::HeartCounters;
pub use identity::DeviceId;
