//! Full Chaya2MQTT firmware application (esp-hal target).
//!
//! Thematic modules: host logic and device backends live in the same folders;
//! device code is gated by `--features device`.

#![cfg_attr(not(test), no_std)]

#[cfg(feature = "device")]
extern crate alloc;

pub mod app;
pub mod audio;
pub mod board;
pub mod display;
#[cfg(feature = "device")]
pub mod http;
pub mod identity;
pub mod input;
pub mod mqtt;
pub mod net;
pub mod ota;
pub mod storage;
pub mod web;
pub mod wifi;


pub use app::{boot, BootReport, Firmware};
pub use identity::{DeviceId, HeartCounters, APP_VERSION};
pub use input::{BatteryService, ButtonService, LedPreset, LedService};
