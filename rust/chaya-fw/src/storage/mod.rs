//! NVS namespaces, platform traits, flash KV (device).

pub mod config;
pub mod platform;

#[cfg(feature = "device")]
pub mod flash_kv;
#[cfg(feature = "device")]
pub mod flash_shared;

pub use config::*;
pub use platform::*;
