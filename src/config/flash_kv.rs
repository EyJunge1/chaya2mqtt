//! Flash map on the last 8 sectors, shared with OTA via [`FlashCell`].
//!
//! The previous flat image (last 2 sectors, magic `0xC4A7A201`) is copied in
//! once. A power cut during that copy can drop settings.

use core::future::Future;
use core::ops::Range;

use embassy_embedded_hal::adapter::BlockingAsync;
use embassy_futures::block_on;
use embedded_storage::nor_flash::{ErrorType, MultiwriteNorFlash, NorFlash, ReadNorFlash};
use esp_storage::{FlashStorage, FlashStorageError};
use sequential_storage::cache::{Cache, Uncached};
use sequential_storage::map::{MapConfig, MapStorage};

use crate::config::flash_shared::FlashCell;
use crate::config::kv_codec::{
    encode_key, key_in_namespace, parse_legacy_store, KvKey, KvVal, LegacyEntry,
    LEGACY_STORE_BYTES, VAL_MAX,
};
use crate::util::platform::KvStore;

/// Pages in the map. The last two still hold a legacy image until migration.
const MAP_PAGES: u32 = 8;

const DATA_LEN: usize = 128;

#[repr(C, align(4))]
struct WordBuf([u8; DATA_LEN]);

type KvFlash = BlockingAsync<SharedNorFlash>;
type KvCache = Cache<Uncached, Uncached, Uncached, KvKey>;

pub struct FlashKv {
    storage: MapStorage<KvKey, KvFlash, KvCache>,
}

struct SharedNorFlash {
    cell: &'static FlashCell,
}

impl ErrorType for SharedNorFlash {
    type Error = FlashStorageError;
}

impl ReadNorFlash for SharedNorFlash {
    const READ_SIZE: usize = FlashStorage::WORD_SIZE as usize;

    fn read(&mut self, offset: u32, bytes: &mut [u8]) -> Result<(), Self::Error> {
        self.cell
            .lock(|f| ReadNorFlash::read(&mut *f.borrow_mut(), offset, bytes))
    }

    fn capacity(&self) -> usize {
        self.cell.lock(|f| ReadNorFlash::capacity(&*f.borrow()))
    }
}

impl NorFlash for SharedNorFlash {
    const WRITE_SIZE: usize = FlashStorage::WORD_SIZE as usize;
    const ERASE_SIZE: usize = FlashStorage::SECTOR_SIZE as usize;

    fn write(&mut self, offset: u32, bytes: &[u8]) -> Result<(), Self::Error> {
        self.cell
            .lock(|f| NorFlash::write(&mut *f.borrow_mut(), offset, bytes))
    }

    fn erase(&mut self, from: u32, to: u32) -> Result<(), Self::Error> {
        self.cell
            .lock(|f| NorFlash::erase(&mut *f.borrow_mut(), from, to))
    }
}

impl MultiwriteNorFlash for SharedNorFlash {}

impl FlashKv {
    pub fn open(flash: &'static FlashCell) -> Self {
        let cap = flash.lock(|f| f.borrow().capacity()) as u32;
        let range = map_range(cap);
        let config = MapConfig::<KvFlash>::try_new(range.clone())
            .unwrap_or_else(|_| panic!("FlashKv: bad range {:#x}..{:#x}", range.start, range.end));
        let legacy = read_legacy(flash, cap);
        let storage = MapStorage::new(
            BlockingAsync::new(SharedNorFlash { cell: flash }),
            config,
            Cache::new_uncached(),
        );
        let mut this = Self { storage };
        if let Some(entries) = legacy {
            esp_println::println!(
                "FlashKv: migrating {} legacy entries @ {:#x}",
                entries.len(),
                range.start
            );
            if drive(this.storage.erase_all()).is_ok() {
                for entry in &entries {
                    if !this.put_blob(entry.ns.as_str(), entry.key.as_str(), &entry.value) {
                        esp_println::println!(
                            "FlashKv: migrate put failed {}/{}",
                            entry.ns,
                            entry.key
                        );
                    }
                }
            } else {
                esp_println::println!("FlashKv: migration erase failed");
            }
        } else {
            esp_println::println!("FlashKv: map @ {:#x}", range.start);
        }
        this
    }
}

impl KvStore for FlashKv {
    fn get_blob(&mut self, ns: &str, key: &str, buf: &mut [u8]) -> Option<usize> {
        let key = encode_key(ns, key)?;
        let mut data = WordBuf([0; DATA_LEN]);
        let value = drive(self.storage.fetch_item::<KvVal>(&mut data.0, &key)).ok()??;
        let n = value.len().min(buf.len());
        buf[..n].copy_from_slice(&value[..n]);
        Some(n)
    }

    fn put_blob(&mut self, ns: &str, key: &str, data: &[u8]) -> bool {
        if data.len() > VAL_MAX {
            return false;
        }
        let Some(key) = encode_key(ns, key) else {
            return false;
        };
        let mut value = KvVal::new();
        if value.extend_from_slice(data).is_err() {
            return false;
        }
        let mut buf = WordBuf([0; DATA_LEN]);
        drive(self.storage.store_item(&mut buf.0, &key, &value)).is_ok()
    }

    fn erase_ns(&mut self, ns: &str) -> bool {
        let mut buf = WordBuf([0; DATA_LEN]);
        let found = drive(async {
            let mut iter = self
                .storage
                .fetch_all_items(&mut buf.0)
                .await
                .map_err(|_| ())?;
            let mut keys = heapless::Vec::<KvKey, 32>::new();
            loop {
                match iter.next::<KvVal>(&mut buf.0).await {
                    Ok(Some((key, _))) => {
                        if key_in_namespace(&key, ns)
                            && !keys.contains(&key)
                            && keys.push(key).is_err()
                        {
                            return Err(());
                        }
                    }
                    Ok(None) => return Ok(keys),
                    Err(_) => return Err(()),
                }
            }
        });
        let Ok(keys) = found else {
            esp_println::println!("FlashKv: erase_ns({ns}) scan failed");
            return false;
        };
        for key in &keys {
            if drive(self.storage.remove_item(&mut buf.0, key)).is_err() {
                esp_println::println!("FlashKv: erase_ns({ns}) remove failed");
                return false;
            }
        }
        true
    }
}

fn map_range(cap: u32) -> Range<u32> {
    let erase = FlashStorage::SECTOR_SIZE;
    let end = cap & !(erase - 1);
    let start = end.saturating_sub(MAP_PAGES * erase);
    start..end
}

fn read_legacy(flash: &'static FlashCell, cap: u32) -> Option<heapless::Vec<LegacyEntry, 24>> {
    let base = cap.saturating_sub(2 * FlashStorage::SECTOR_SIZE);
    let mut buf = [0u8; LEGACY_STORE_BYTES];
    let ok = flash.lock(|f| f.borrow_mut().read(base, &mut buf).is_ok());
    if !ok {
        return None;
    }
    parse_legacy_store(&buf)
}

/// `sequential-storage` is async; the NOR adapter finishes inside one poll.
fn drive<F: Future>(fut: F) -> F::Output {
    block_on(fut)
}
