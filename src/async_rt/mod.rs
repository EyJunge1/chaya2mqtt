//! Embassy tasks + shared device state.

pub mod audio;
pub mod battery;
pub mod button;
pub mod dhcp;
pub mod epd;
pub mod heart;
pub mod mqtt;
pub mod net;
pub mod ntp;
pub mod ota;
pub mod ota_health;
pub mod state;
pub mod util;
pub mod wifi;

pub use util::MonoClock;
