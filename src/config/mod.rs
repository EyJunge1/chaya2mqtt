//! App config, NVS namespaces, flash KV (device) — mirrors C++ `src/config/`.

pub mod app_config;
pub mod kv_codec;
pub mod ui_prefs;
pub mod version;

#[cfg(feature = "device")]
pub mod flash_kv;
#[cfg(feature = "device")]
pub mod flash_shared;

pub use app_config::*;
pub use ui_prefs::UiPrefs;
pub use version::APP_VERSION;
