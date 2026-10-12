//! Shared helpers / platform traits — mirrors C++ `src/util/`.

pub mod platform;
#[cfg(feature = "device")]
pub mod rng;

pub use platform::*;
#[cfg(feature = "device")]
pub use rng::EspRng;
