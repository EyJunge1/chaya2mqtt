//! Shared SPI flash (FlashKv + OTA).

use core::cell::RefCell;
use embassy_sync::blocking_mutex::{raw::CriticalSectionRawMutex, Mutex};
use embedded_storage::{ReadStorage, Storage};
use esp_storage::{FlashStorage, FlashStorageError};

pub type FlashCell = Mutex<CriticalSectionRawMutex, RefCell<FlashStorage<'static>>>;

/// Sync Storage proxy over a critical-section mutex (for `esp-hal-ota`).
pub struct FlashProxy(pub &'static FlashCell);

impl ReadStorage for FlashProxy {
    type Error = FlashStorageError;

    fn read(&mut self, offset: u32, bytes: &mut [u8]) -> Result<(), Self::Error> {
        self.0.lock(|f| f.borrow_mut().read(offset, bytes))
    }

    fn capacity(&self) -> usize {
        self.0.lock(|f| f.borrow().capacity())
    }
}

impl Storage for FlashProxy {
    fn write(&mut self, offset: u32, bytes: &[u8]) -> Result<(), Self::Error> {
        self.0.lock(|f| f.borrow_mut().write(offset, bytes))
    }
}
