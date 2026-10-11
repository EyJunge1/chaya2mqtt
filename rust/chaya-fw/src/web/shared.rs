//! Shared HTTP handler state.

use embassy_net::Stack;
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::mutex::Mutex;
use embassy_sync::signal::Signal;

use crate::audio::AudioConfig;
use crate::ota::client::OtaStatus;
use crate::config::flash_kv::FlashKv;
use crate::wifi::scan::ScanCache;
use crate::wifi::WlanConfig;
use chaya_pure::audio::AudioKind;

pub type Cs = CriticalSectionRawMutex;
pub type KvMutex = Mutex<Cs, FlashKv>;

/// Shared signals/state the HTTP handlers need from `main`.
pub struct HttpShared {
    pub kv: &'static KvMutex,
    pub device_id: &'static str,
    pub ap_mode: bool,
    pub ap_psk: Option<&'static str>,
    pub heart_tx: &'static Mutex<Cs, i32>,
    pub heart_rx: &'static Mutex<Cs, i32>,
    pub mqtt_ok: &'static Mutex<Cs, bool>,
    pub bat_pct: &'static Mutex<Cs, i32>,
    pub bat_mv: &'static Mutex<Cs, i32>,
    pub audio_cfg: &'static Mutex<Cs, AudioConfig>,
    pub ota_status: &'static Mutex<Cs, OtaStatus>,
    pub wifi_scan: &'static Mutex<Cs, ScanCache>,
    pub wifi_pending: &'static Mutex<Cs, WlanConfig>,
    pub wifi_test_state: &'static Mutex<Cs, heapless::String<12>>,
    pub wifi_test_ssid: &'static Mutex<Cs, heapless::String<33>>,
    pub wifi_test_owns: &'static Mutex<Cs, bool>,
    pub sta_ssid: &'static Mutex<Cs, heapless::String<33>>,
    pub sta_rssi: &'static Mutex<Cs, i32>,
    pub stack: Stack<'static>,
    pub heart_bump: &'static Signal<Cs, i32>,
    pub audio_beep: &'static Signal<Cs, AudioKind>,
    pub heart_dirty: &'static Signal<Cs, ()>,
    pub epd_redraw: &'static Signal<Cs, ()>,
    pub reboot: &'static Signal<Cs, ()>,
    pub ota_check: &'static Signal<Cs, ()>,
    pub ota_install: &'static Signal<Cs, ()>,
    pub scan_kick: &'static Signal<Cs, ()>,
    pub wifi_test_kick: &'static Signal<Cs, ()>,
}

