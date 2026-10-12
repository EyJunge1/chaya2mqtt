//! Cached Wi-Fi AP scan snapshot (OpenAPI `WifiScanSnapshot`).

use alloc::string::String;
use crate::core::constants::WIFI_SSID_MAX_LEN;
use crate::core::wifi::{wifi_scan_service_may_start_kick, wifi_scan_service_should_defer_kick};

pub const WIFI_SCAN_CACHE_MAX: usize = 24;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ScanStatus {
    Idle,
    Pending,
    Ready,
    Failed,
}

impl ScanStatus {
    pub fn as_str(self) -> &'static str {
        match self {
            Self::Idle => "idle",
            Self::Pending => "pending",
            Self::Ready => "ready",
            Self::Failed => "failed",
        }
    }
}

#[derive(Clone, Debug, Default)]
pub struct ScanAp {
    pub ssid: heapless::String<WIFI_SSID_MAX_LEN>,
    pub rssi: i8,
    pub open: bool,
}

#[derive(Clone, Debug)]
pub struct ScanCache {
    pub status: ScanStatus,
    pub aps: heapless::Vec<ScanAp, WIFI_SCAN_CACHE_MAX>,
}

impl Default for ScanCache {
    fn default() -> Self {
        Self {
            status: ScanStatus::Idle,
            aps: heapless::Vec::new(),
        }
    }
}

impl ScanCache {
    pub fn request_kick(&mut self, connection_test_owns_radio: bool, ota_busy: bool) -> Result<(), &'static str> {
        if wifi_scan_service_should_defer_kick(connection_test_owns_radio, ota_busy) {
            return Err("busy");
        }
        if !wifi_scan_service_may_start_kick(self.status == ScanStatus::Pending) {
            return Ok(());
        }
        self.status = ScanStatus::Pending;
        Ok(())
    }

    pub fn to_json(&self) -> String {
        let mut s = String::new();
        s.push_str("{\"status\":\"");
        s.push_str(self.status.as_str());
        s.push('"');
        if self.status == ScanStatus::Ready {
            s.push_str(",\"aps\":[");
            for (i, ap) in self.aps.iter().enumerate() {
                if i > 0 {
                    s.push(',');
                }
                s.push_str("{\"ssid\":\"");
                push_json_escaped(&mut s, ap.ssid.as_str());
                s.push_str("\",\"rssi\":");
                push_i32(&mut s, ap.rssi as i32);
                s.push_str(",\"open\":");
                s.push_str(if ap.open { "true" } else { "false" });
                s.push('}');
            }
            s.push(']');
        }
        s.push('}');
        s
    }
}

fn push_json_escaped(out: &mut String, input: &str) {
    for c in input.chars() {
        match c {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            c if c < ' ' => {}
            c => out.push(c),
        }
    }
}

fn push_i32(out: &mut String, mut n: i32) {
    if n < 0 {
        out.push('-');
        n = -n;
    }
    if n == 0 {
        out.push('0');
        return;
    }
    let mut digs = heapless::Vec::<u8, 16>::new();
    while n > 0 {
        let _ = digs.push(b'0' + (n % 10) as u8);
        n /= 10;
    }
    for d in digs.iter().rev() {
        out.push(*d as char);
    }
}
