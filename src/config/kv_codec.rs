//! Key encoding and the legacy flat FlashKv image.
//!
//! Keys are `ns_len | key_len | ns | key` inside a `heapless::Vec` so
//! `sequential-storage`'s heapless `Key` impl can store them as-is.
//! Values are raw bytes (max 64), length tracked by the map item header.

use heapless::{String, Vec};

pub const NS_MAX: usize = 16;
pub const KEY_MAX: usize = 16;
pub const VAL_MAX: usize = 64;

/// `ns_len + key_len + ns + key`.
pub const KEY_CAP: usize = 2 + NS_MAX + KEY_MAX;

const LEGACY_MAGIC: u32 = 0xC4A7_A201;
const LEGACY_VERSION: u32 = 1;
const LEGACY_MAX_ENTRIES: usize = 24;
const ENTRY_BYTES: usize = NS_MAX + KEY_MAX + 2 + VAL_MAX;
const HEADER_BYTES: usize = 16;

pub const LEGACY_STORE_BYTES: usize = HEADER_BYTES + LEGACY_MAX_ENTRIES * ENTRY_BYTES;

pub type KvKey = Vec<u8, KEY_CAP>;
pub type KvVal = Vec<u8, VAL_MAX>;

pub struct LegacyEntry {
    pub ns: String<NS_MAX>,
    pub key: String<KEY_MAX>,
    pub value: KvVal,
}

pub fn encode_key(ns: &str, key: &str) -> Option<KvKey> {
    if ns.len() > NS_MAX || key.len() > KEY_MAX {
        return None;
    }
    let mut out = KvKey::new();
    out.push(ns.len() as u8).ok()?;
    out.push(key.len() as u8).ok()?;
    out.extend_from_slice(ns.as_bytes()).ok()?;
    out.extend_from_slice(key.as_bytes()).ok()?;
    Some(out)
}

pub fn key_in_namespace(raw: &[u8], ns: &str) -> bool {
    split_key(raw).is_some_and(|(entry_ns, _)| entry_ns == ns)
}

fn split_key(raw: &[u8]) -> Option<(&str, &str)> {
    if raw.len() < 2 {
        return None;
    }
    let ns_len = raw[0] as usize;
    let key_len = raw[1] as usize;
    let rest = raw.get(2..)?;
    if rest.len() != ns_len + key_len {
        return None;
    }
    let ns = core::str::from_utf8(rest.get(..ns_len)?).ok()?;
    let key = core::str::from_utf8(rest.get(ns_len..)?).ok()?;
    Some((ns, key))
}

/// Decode the pre-`sequential-storage` image.
///
/// `None` means the header is not that format. A matching header always
/// returns `Some`, skipping slots that are empty or unreadable.
pub fn parse_legacy_store(buf: &[u8]) -> Option<Vec<LegacyEntry, LEGACY_MAX_ENTRIES>> {
    if buf.len() < LEGACY_STORE_BYTES {
        return None;
    }
    let magic = u32::from_le_bytes(buf[0..4].try_into().ok()?);
    let ver = u32::from_le_bytes(buf[4..8].try_into().ok()?);
    if magic != LEGACY_MAGIC || ver != LEGACY_VERSION {
        return None;
    }
    let mut out = Vec::new();
    for i in 0..LEGACY_MAX_ENTRIES {
        let off = HEADER_BYTES + i * ENTRY_BYTES;
        if let Some(entry) = parse_legacy_entry(&buf[off..off + ENTRY_BYTES]) {
            let _ = out.push(entry);
        }
    }
    Some(out)
}

fn parse_legacy_entry(entry: &[u8]) -> Option<LegacyEntry> {
    if entry.len() < ENTRY_BYTES {
        return None;
    }
    let len = u16::from_le_bytes(
        entry[NS_MAX + KEY_MAX..NS_MAX + KEY_MAX + 2]
            .try_into()
            .ok()?,
    ) as usize;
    if len == 0 || len > VAL_MAX {
        return None;
    }
    let ns = cstr_string::<NS_MAX>(&entry[..NS_MAX])?;
    let key = cstr_string::<KEY_MAX>(&entry[NS_MAX..NS_MAX + KEY_MAX])?;
    let mut value = KvVal::new();
    value
        .extend_from_slice(&entry[NS_MAX + KEY_MAX + 2..NS_MAX + KEY_MAX + 2 + len])
        .ok()?;
    Some(LegacyEntry { ns, key, value })
}

fn cstr_string<const N: usize>(buf: &[u8]) -> Option<String<N>> {
    let end = buf.iter().position(|&b| b == 0).unwrap_or(buf.len());
    let text = core::str::from_utf8(&buf[..end]).ok()?;
    String::try_from(text).ok()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn legacy_image(entries: &[(&str, &str, &[u8])]) -> [u8; LEGACY_STORE_BYTES] {
        let mut buf = [0u8; LEGACY_STORE_BYTES];
        buf[0..4].copy_from_slice(&LEGACY_MAGIC.to_le_bytes());
        buf[4..8].copy_from_slice(&LEGACY_VERSION.to_le_bytes());
        for (i, (ns, key, val)) in entries.iter().enumerate() {
            let off = HEADER_BYTES + i * ENTRY_BYTES;
            buf[off..off + ns.len()].copy_from_slice(ns.as_bytes());
            buf[off + NS_MAX..off + NS_MAX + key.len()].copy_from_slice(key.as_bytes());
            buf[off + NS_MAX + KEY_MAX..off + NS_MAX + KEY_MAX + 2]
                .copy_from_slice(&(val.len() as u16).to_le_bytes());
            let data = off + NS_MAX + KEY_MAX + 2;
            buf[data..data + val.len()].copy_from_slice(val);
        }
        buf
    }

    #[test]
    fn key_roundtrip_and_namespace() {
        let key = encode_key("wifi", "ssid").unwrap();
        assert!(key_in_namespace(&key, "wifi"));
        assert!(!key_in_namespace(&key, "mqtt"));
        assert!(encode_key("this-namespace-is-too-long", "k").is_none());
    }

    #[test]
    fn legacy_image_keeps_entries_and_skips_empty_slots() {
        let img = legacy_image(&[("wifi", "ssid", b"home"), ("cfg", "device_id", b"abc\0")]);
        let parsed = parse_legacy_store(&img).unwrap();
        assert_eq!(parsed.len(), 2);
        assert_eq!(parsed[0].ns.as_str(), "wifi");
        assert_eq!(parsed[0].key.as_str(), "ssid");
        assert_eq!(parsed[0].value.as_slice(), b"home");
        assert_eq!(parsed[1].value.as_slice(), b"abc\0");
    }

    #[test]
    fn legacy_keeps_good_slots_when_one_is_garbage() {
        let mut img = legacy_image(&[("wifi", "ssid", b"home"), ("cfg", "led_en", b"xxxx")]);
        let off = HEADER_BYTES + ENTRY_BYTES;
        img[off] = 0xff;
        let parsed = parse_legacy_store(&img).unwrap();
        assert_eq!(parsed.len(), 1);
        assert_eq!(parsed[0].key.as_str(), "ssid");
    }

    #[test]
    fn legacy_rejects_other_magic() {
        let mut img = legacy_image(&[]);
        img[0] = 0;
        assert!(parse_legacy_store(&img).is_none());
    }
}
