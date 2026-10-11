//! E-Paper UI — host state + panel driver (nah zusammen).

mod state;
pub use state::*;

#[cfg(feature = "device")]
pub mod epd;
#[cfg(feature = "device")]
pub mod qr;
