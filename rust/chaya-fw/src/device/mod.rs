//! Device-only backends (compiled with `--features device`).

pub mod audio_hw;
pub mod battery_adc;
pub mod epd;
pub mod flash_kv;
pub mod http;
pub mod mqtt_tcp;
pub mod ota_client;
pub mod spa;
