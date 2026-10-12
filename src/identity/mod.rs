//! Device identity (6 hex via `chaya-pure`), heart counters, version.

use crate::config::NS_CFG;
use crate::util::platform::KvStore;
use chaya_pure::constants::{device_id_syntax_ok, DEVICE_ID_BUF_LEN, DEVICE_ID_HEX_LEN};
use chaya_pure::identity::device_id_format_from_bytes;

const KEY_DEVICE_ID: &str = "device_id";

#[derive(Clone, Debug)]
pub struct DeviceId {
    /// NUL-terminated 6 hex + NUL in DEVICE_ID_BUF_LEN bytes.
    pub bytes: [u8; DEVICE_ID_BUF_LEN],
}

impl Default for DeviceId {
    fn default() -> Self {
        Self {
            bytes: [0; DEVICE_ID_BUF_LEN],
        }
    }
}

impl DeviceId {
    pub fn as_str(&self) -> &str {
        let end = self
            .bytes
            .iter()
            .position(|&c| c == 0)
            .unwrap_or(DEVICE_ID_HEX_LEN)
            .min(DEVICE_ID_HEX_LEN);
        core::str::from_utf8(&self.bytes[..end]).unwrap_or("")
    }

    pub fn from_random_bytes(raw: [u8; 3]) -> Result<Self, ()> {
        let mut out = [0u8; DEVICE_ID_BUF_LEN];
        device_id_format_from_bytes(&raw, &mut out).map_err(|_| ())?;
        Ok(Self { bytes: out })
    }

    pub fn load_or_create(kv: &mut dyn KvStore, random3: [u8; 3]) -> Self {
        let mut buf = [0u8; DEVICE_ID_BUF_LEN];
        if let Some(n) = kv.get_blob(NS_CFG, KEY_DEVICE_ID, &mut buf) {
            if n >= DEVICE_ID_HEX_LEN {
                if let Ok(s) = core::str::from_utf8(&buf[..DEVICE_ID_HEX_LEN]) {
                    if device_id_syntax_ok(s) {
                        buf[DEVICE_ID_HEX_LEN] = 0;
                        return Self { bytes: buf };
                    }
                }
            }
        }
        let id = Self::from_random_bytes(random3).unwrap_or_default();
        let _ = kv.put_blob(NS_CFG, KEY_DEVICE_ID, &id.bytes[..DEVICE_ID_HEX_LEN]);
        id
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::util::platform::SimKv;

    #[test]
    fn create_and_reload() {
        let mut kv = SimKv::default();
        let a = DeviceId::load_or_create(&mut kv, [0xab, 0xcd, 0xef]);
        assert_eq!(a.as_str(), "abcdef");
        let b = DeviceId::load_or_create(&mut kv, [0, 0, 0]);
        assert_eq!(b.as_str(), "abcdef");
    }
}
