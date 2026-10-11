//! Flash-backed KV using the last two sectors (shared FlashCell).

use crate::storage::flash_shared::FlashCell;
use crate::storage::platform::KvStore;
use esp_storage::FlashStorage;

const MAGIC: u32 = 0xC4A7_A201;
const VERSION: u32 = 1;
const MAX_ENTRIES: usize = 24;
const NS_LEN: usize = 16;
const KEY_LEN: usize = 16;
const VAL_LEN: usize = 64;
const ENTRY_BYTES: usize = NS_LEN + KEY_LEN + 2 + VAL_LEN;
const HEADER_BYTES: usize = 16;
const STORE_BYTES: usize = HEADER_BYTES + MAX_ENTRIES * ENTRY_BYTES;

#[derive(Clone, Copy)]
struct Entry {
    ns: [u8; NS_LEN],
    key: [u8; KEY_LEN],
    len: u16,
    data: [u8; VAL_LEN],
}

impl Entry {
    fn empty() -> Self {
        Self {
            ns: [0; NS_LEN],
            key: [0; KEY_LEN],
            len: 0,
            data: [0; VAL_LEN],
        }
    }

    fn matches(&self, ns: &str, key: &str) -> bool {
        self.len > 0 && cstr_eq(&self.ns, ns) && cstr_eq(&self.key, key)
    }
}

fn cstr_eq(buf: &[u8], s: &str) -> bool {
    let b = s.as_bytes();
    if b.len() > buf.len() {
        return false;
    }
    &buf[..b.len()] == b && buf[b.len()..].iter().all(|&c| c == 0)
}

fn put_cstr(dst: &mut [u8], s: &str) -> bool {
    let b = s.as_bytes();
    if b.len() > dst.len() {
        return false;
    }
    dst.fill(0);
    dst[..b.len()].copy_from_slice(b);
    true
}

pub struct FlashKv {
    flash: &'static FlashCell,
    base: u32,
    entries: [Entry; MAX_ENTRIES],
    dirty: bool,
}

impl FlashKv {
    pub fn open(flash: &'static FlashCell) -> Self {
        let (cap, read_ok, buf) = flash.lock(|f| {
            let mut flash = f.borrow_mut();
            let cap = flash.capacity() as u32;
            let mut buf = [0u8; STORE_BYTES];
            let ok = flash
                .read(cap.saturating_sub(2 * FlashStorage::SECTOR_SIZE), &mut buf)
                .is_ok();
            (cap, ok, buf)
        });
        let base = cap.saturating_sub(2 * FlashStorage::SECTOR_SIZE);
        let mut entries = [Entry::empty(); MAX_ENTRIES];
        let mut ok = false;
        if read_ok {
            let magic = u32::from_le_bytes(buf[0..4].try_into().unwrap());
            let ver = u32::from_le_bytes(buf[4..8].try_into().unwrap());
            if magic == MAGIC && ver == VERSION {
                for i in 0..MAX_ENTRIES {
                    let off = HEADER_BYTES + i * ENTRY_BYTES;
                    let mut e = Entry::empty();
                    e.ns.copy_from_slice(&buf[off..off + NS_LEN]);
                    e.key
                        .copy_from_slice(&buf[off + NS_LEN..off + NS_LEN + KEY_LEN]);
                    e.len = u16::from_le_bytes(
                        buf[off + NS_LEN + KEY_LEN..off + NS_LEN + KEY_LEN + 2]
                            .try_into()
                            .unwrap(),
                    );
                    e.data.copy_from_slice(
                        &buf[off + NS_LEN + KEY_LEN + 2..off + NS_LEN + KEY_LEN + 2 + VAL_LEN],
                    );
                    if e.len as usize > VAL_LEN {
                        e.len = 0;
                    }
                    entries[i] = e;
                }
                ok = true;
            }
        }
        if !ok {
            esp_println::println!("FlashKv: empty/new store @ {base:#x}");
        } else {
            esp_println::println!("FlashKv: loaded @ {base:#x}");
        }
        Self {
            flash,
            base,
            entries,
            dirty: false,
        }
    }

    fn flush(&mut self) -> bool {
        if !self.dirty {
            return true;
        }
        let mut buf = [0u8; STORE_BYTES];
        buf[0..4].copy_from_slice(&MAGIC.to_le_bytes());
        buf[4..8].copy_from_slice(&VERSION.to_le_bytes());
        for i in 0..MAX_ENTRIES {
            let off = HEADER_BYTES + i * ENTRY_BYTES;
            let e = &self.entries[i];
            buf[off..off + NS_LEN].copy_from_slice(&e.ns);
            buf[off + NS_LEN..off + NS_LEN + KEY_LEN].copy_from_slice(&e.key);
            buf[off + NS_LEN + KEY_LEN..off + NS_LEN + KEY_LEN + 2]
                .copy_from_slice(&e.len.to_le_bytes());
            buf[off + NS_LEN + KEY_LEN + 2..off + NS_LEN + KEY_LEN + 2 + VAL_LEN]
                .copy_from_slice(&e.data);
        }
        let ok = self
            .flash
            .lock(|f| f.borrow_mut().write(self.base, &buf).is_ok());
        if ok {
            self.dirty = false;
        }
        ok
    }

    fn find_slot(&self, ns: &str, key: &str) -> Option<usize> {
        self.entries.iter().position(|e| e.matches(ns, key))
    }

    fn find_free(&self) -> Option<usize> {
        self.entries.iter().position(|e| e.len == 0)
    }
}

impl KvStore for FlashKv {
    fn get_blob(&mut self, ns: &str, key: &str, buf: &mut [u8]) -> Option<usize> {
        let i = self.find_slot(ns, key)?;
        let e = &self.entries[i];
        let n = (e.len as usize).min(buf.len());
        buf[..n].copy_from_slice(&e.data[..n]);
        Some(n)
    }

    fn put_blob(&mut self, ns: &str, key: &str, data: &[u8]) -> bool {
        if data.len() > VAL_LEN {
            return false;
        }
        let slot = self
            .find_slot(ns, key)
            .or_else(|| self.find_free())
            .unwrap_or(usize::MAX);
        if slot == usize::MAX {
            return false;
        }
        let e = &mut self.entries[slot];
        if !put_cstr(&mut e.ns, ns) || !put_cstr(&mut e.key, key) {
            return false;
        }
        e.data.fill(0);
        e.data[..data.len()].copy_from_slice(data);
        e.len = data.len() as u16;
        self.dirty = true;
        self.flush()
    }

    fn get_i32(&mut self, ns: &str, key: &str) -> Option<i32> {
        let mut buf = [0u8; 4];
        let n = self.get_blob(ns, key, &mut buf)?;
        if n != 4 {
            return None;
        }
        Some(i32::from_le_bytes(buf))
    }

    fn put_i32(&mut self, ns: &str, key: &str, v: i32) -> bool {
        self.put_blob(ns, key, &v.to_le_bytes())
    }

    fn erase_ns(&mut self, ns: &str) -> bool {
        for e in &mut self.entries {
            if e.len > 0 && cstr_eq(&e.ns, ns) {
                *e = Entry::empty();
                self.dirty = true;
            }
        }
        self.flush()
    }
}
