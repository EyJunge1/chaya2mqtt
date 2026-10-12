//! Cached Wi-Fi AP scan snapshot (OpenAPI `WifiScanSnapshot`).

use crate::core::constants::WIFI_SSID_MAX_LEN;
use crate::core::wifi::{wifi_scan_service_may_start_kick, wifi_scan_service_should_defer_kick};
use alloc::string::String;
use alloc::vec::Vec;
use serde::Serialize;

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
    pub fn request_kick(
        &mut self,
        connection_test_owns_radio: bool,
        ota_busy: bool,
    ) -> Result<(), &'static str> {
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
        #[derive(Serialize)]
        struct Ap<'a> {
            ssid: &'a str,
            rssi: i8,
            open: bool,
        }
        #[derive(Serialize)]
        struct Snapshot<'a> {
            status: &'static str,
            #[serde(skip_serializing_if = "Option::is_none")]
            aps: Option<Vec<Ap<'a>>>,
        }

        let aps = if self.status == ScanStatus::Ready {
            Some(
                self.aps
                    .iter()
                    .map(|ap| Ap {
                        ssid: ap.ssid.as_str(),
                        rssi: ap.rssi,
                        open: ap.open,
                    })
                    .collect(),
            )
        } else {
            None
        };

        let snap = Snapshot {
            status: self.status.as_str(),
            aps,
        };
        for size in [256usize, 512, 1024, 2048, 4096] {
            let mut buf = alloc::vec![0u8; size];
            if let Ok(n) = serde_json_core::to_slice(&snap, &mut buf) {
                return match core::str::from_utf8(&buf[..n]) {
                    Ok(s) => String::from(s),
                    Err(_) => String::from("{\"status\":\"failed\"}"),
                };
            }
        }
        String::from("{\"status\":\"failed\"}")
    }
}
