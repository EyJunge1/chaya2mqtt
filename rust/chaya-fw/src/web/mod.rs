//! Admin HTTP / SPA — mirrors C++ `src/web/`.

pub mod admin_state;
pub use admin_state::*;

#[cfg(feature = "device")]
pub mod form;
#[cfg(feature = "device")]
pub mod json;
#[cfg(feature = "device")]
pub mod routes;
#[cfg(feature = "device")]
pub mod server;
#[cfg(feature = "device")]
pub mod shared;
#[cfg(feature = "device")]
pub mod spa;
#[cfg(feature = "device")]
pub mod sse;

#[cfg(feature = "device")]
pub use server::run_admin_http;
#[cfg(feature = "device")]
pub use shared::HttpShared;
