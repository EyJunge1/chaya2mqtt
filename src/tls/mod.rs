//! TLS client backends for MQTT and OTA HTTPS.
//!
//! - Default (`device`): `embedded-tls` + host-presence pin (no CA chain).
//! - Optional (`device,mbedtls`): `mbedtls-rs` + Mozilla roots via `mozilla-root-ca`.

#[cfg(not(feature = "mbedtls"))]
mod host_pin;
#[cfg(feature = "mbedtls")]
mod mbedtls_ca;

#[cfg(not(feature = "mbedtls"))]
pub use host_pin::{backend_name, connect, HostPinProvider, HostPresenceVerifier, TlsStream};

#[cfg(feature = "mbedtls")]
pub use mbedtls_ca::{backend_name, connect, TlsStream};
