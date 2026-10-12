//! Shared `esp_hal` RNG adapters for `rand_core` 0.6 (TLS/MQTT/OTA) and 0.10 (mDNS/mbedtls).

use rand_core::{CryptoRng, Error as RandError, RngCore};

fn fill_from_hal(rng: &mut esp_hal::rng::Rng, dest: &mut [u8]) {
    for chunk in dest.chunks_mut(4) {
        let r = rng.random().to_le_bytes();
        chunk.copy_from_slice(&r[..chunk.len()]);
    }
}

/// `rand_core` 0.6 adapter (embedded-tls, MQTT, OTA).
pub struct EspRng(pub esp_hal::rng::Rng);

impl RngCore for EspRng {
    fn next_u32(&mut self) -> u32 {
        self.0.random()
    }
    fn next_u64(&mut self) -> u64 {
        ((self.next_u32() as u64) << 32) | self.next_u32() as u64
    }
    fn fill_bytes(&mut self, dest: &mut [u8]) {
        fill_from_hal(&mut self.0, dest);
    }
    fn try_fill_bytes(&mut self, dest: &mut [u8]) -> Result<(), RandError> {
        self.fill_bytes(dest);
        Ok(())
    }
}

impl CryptoRng for EspRng {}

/// `rand_core` 0.10 adapter (edge-mdns, mbedtls-rs).
pub struct EspRng10(pub esp_hal::rng::Rng);

impl rand_core10::TryRng for EspRng10 {
    type Error = core::convert::Infallible;

    fn try_next_u32(&mut self) -> Result<u32, Self::Error> {
        Ok(self.0.random())
    }
    fn try_next_u64(&mut self) -> Result<u64, Self::Error> {
        Ok(((self.0.random() as u64) << 32) | self.0.random() as u64)
    }
    fn try_fill_bytes(&mut self, dest: &mut [u8]) -> Result<(), Self::Error> {
        fill_from_hal(&mut self.0, dest);
        Ok(())
    }
}

impl rand_core10::TryCryptoRng for EspRng10 {}
