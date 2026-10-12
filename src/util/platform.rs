//! Thin platform traits so the app builds on host (`Sim*`) and on device (esp-hal).

/// Digital output by GPIO number (board pin constants).
pub trait GpioOut {
    fn set_output(&mut self, pin: u8, high: bool);
}

/// Digital input by GPIO number.
pub trait GpioIn {
    fn get_input(&mut self, pin: u8) -> bool;
}

/// Millisecond clock (wrap-safe u32 like Arduino `millis`).
pub trait ClockMs {
    fn now_ms(&self) -> u32;
}

/// Non-volatile store (Preferences/NVS stand-in).
pub trait KvStore {
    fn get_blob(&mut self, ns: &str, key: &str, buf: &mut [u8]) -> Option<usize>;
    fn put_blob(&mut self, ns: &str, key: &str, data: &[u8]) -> bool;
    fn get_i32(&mut self, ns: &str, key: &str) -> Option<i32>;
    fn put_i32(&mut self, ns: &str, key: &str, v: i32) -> bool;
    fn erase_ns(&mut self, ns: &str) -> bool;
}

/// Host/sim GPIO: records last driven level per pin.
pub struct SimGpio {
    levels: [Option<bool>; 64],
}

impl Default for SimGpio {
    fn default() -> Self {
        Self { levels: [None; 64] }
    }
}

impl SimGpio {
    pub fn get(&self, pin: u8) -> Option<bool> {
        self.levels.get(pin as usize).copied().flatten()
    }

    pub fn set_input_level(&mut self, pin: u8, high: bool) {
        if let Some(slot) = self.levels.get_mut(pin as usize) {
            *slot = Some(high);
        }
    }
}

impl GpioOut for SimGpio {
    fn set_output(&mut self, pin: u8, high: bool) {
        if let Some(slot) = self.levels.get_mut(pin as usize) {
            *slot = Some(high);
        }
    }
}

impl GpioIn for SimGpio {
    fn get_input(&mut self, pin: u8) -> bool {
        self.get(pin).unwrap_or(true)
    }
}

#[derive(Default)]
pub struct SimClock {
    pub ms: u32,
}

impl ClockMs for SimClock {
    fn now_ms(&self) -> u32 {
        self.ms
    }
}

/// In-memory KV for host tests (single flat map of ns/key).
#[derive(Default)]
pub struct SimKv {
    entries: heapless::Vec<
        (
            heapless::String<16>,
            heapless::String<16>,
            heapless::Vec<u8, 64>,
        ),
        32,
    >,
}

impl KvStore for SimKv {
    fn get_blob(&mut self, ns: &str, key: &str, buf: &mut [u8]) -> Option<usize> {
        for (n, k, v) in &self.entries {
            if n.as_str() == ns && k.as_str() == key {
                let ncopy = v.len().min(buf.len());
                buf[..ncopy].copy_from_slice(&v[..ncopy]);
                return Some(ncopy);
            }
        }
        None
    }

    fn put_blob(&mut self, ns: &str, key: &str, data: &[u8]) -> bool {
        let Ok(ns_s) = heapless::String::try_from(ns) else {
            return false;
        };
        let Ok(key_s) = heapless::String::try_from(key) else {
            return false;
        };
        let mut v = heapless::Vec::new();
        if v.extend_from_slice(data).is_err() {
            return false;
        }
        if let Some((_, _, slot)) = self
            .entries
            .iter_mut()
            .find(|(n, k, _)| n.as_str() == ns && k.as_str() == key)
        {
            *slot = v;
            return true;
        }
        self.entries.push((ns_s, key_s, v)).is_ok()
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
        self.entries.retain(|(n, _, _)| n.as_str() != ns);
        true
    }
}
