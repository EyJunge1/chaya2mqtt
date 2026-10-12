//! SSE producer dirty bits (PERF-03) — mirrors former C++ `sse_dirty`.
//!
//! Producers call [`sse_mark`]; the `/events` loop consumes via [`sse_consume`].
//! Uses a critical-section cell (not `AtomicU32::exchange`) so Xtensa device
//! builds work without atomic RMW for `u32`.

use core::cell::Cell;

use critical_section::Mutex;

/// Pending domain bits (`SSE_CHAYA` … `SSE_DEVICE`).
pub static SSE_PENDING: Mutex<Cell<u32>> = Mutex::new(Cell::new(0));

/// OR pending bits (no-op when `bits == 0`).
pub fn sse_mark(bits: u32) {
    if bits == 0 {
        return;
    }
    critical_section::with(|cs| {
        let cell = SSE_PENDING.borrow(cs);
        cell.set(cell.get() | bits);
    });
}

/// Atomically take and clear pending bits.
pub fn sse_consume() -> u32 {
    critical_section::with(|cs| {
        let cell = SSE_PENDING.borrow(cs);
        let pending = cell.get();
        cell.set(0);
        pending
    })
}
