//! Shared Embassy signals / mutexes for device tasks.

use chaya_fw::audio::AudioConfig;
use chaya_fw::input::LedPreset;
use chaya_fw::net::ntp::WallClock;
use chaya_fw::ota::client::OtaStatus;
use chaya_fw::storage::flash_kv::FlashKv;
use chaya_fw::wifi::scan::{ScanCache, ScanStatus};
use chaya_fw::wifi::{IpMode, WlanConfig};
use chaya_pure::audio::AudioKind;
use embassy_sync::{
    blocking_mutex::raw::CriticalSectionRawMutex,
    mutex::Mutex,
    signal::Signal,
};

pub type Cs = CriticalSectionRawMutex;

pub static HEART_BUMP: Signal<Cs, i32> = Signal::new();
pub static AUDIO_BEEP: Signal<Cs, AudioKind> = Signal::new();
pub static HEART_DIRTY: Signal<Cs, ()> = Signal::new();
pub static EPD_REDRAW: Signal<Cs, ()> = Signal::new();
pub static REBOOT: Signal<Cs, ()> = Signal::new();
pub static OTA_CHECK: Signal<Cs, ()> = Signal::new();
pub static OTA_INSTALL: Signal<Cs, ()> = Signal::new();
pub static SCAN_KICK: Signal<Cs, ()> = Signal::new();
pub static WIFI_TEST_KICK: Signal<Cs, ()> = Signal::new();
pub static HEART_RX: Mutex<Cs, i32> = Mutex::new(0);
pub static MQTT_OK: Mutex<Cs, bool> = Mutex::new(false);
pub static BAT_PCT: Mutex<Cs, i32> = Mutex::new(-1);
pub static BAT_MV: Mutex<Cs, i32> = Mutex::new(0);
pub static HEART_TX: Mutex<Cs, i32> = Mutex::new(0);
pub static TIME_SYNCED: Mutex<Cs, bool> = Mutex::new(false);
pub static WALL_CLOCK: Mutex<Cs, WallClock> = Mutex::new(WallClock {
    synced_at: None,
    unix_at_sync: 0,
});
pub static AUDIO_CFG: Mutex<Cs, AudioConfig> = Mutex::new(AudioConfig {
    volume: 70,
    tx_enabled: true,
    rx_enabled: true,
    quiet_start: 0,
    quiet_end: 0,
});
pub static LED_PRESET: Signal<Cs, LedPreset> = Signal::new();
pub static MQTT_UP: Signal<Cs, ()> = Signal::new();
pub type KvMutex = Mutex<Cs, FlashKv>;
pub static OTA_STATUS: Mutex<Cs, OtaStatus> = Mutex::new(OtaStatus {
    last_tag: heapless::String::new(),
    last_error: heapless::String::new(),
    channel: heapless::String::new(),
    checking: false,
    installing: false,
    flash_ready: false,
});
pub static WIFI_SCAN: Mutex<Cs, ScanCache> = Mutex::new(ScanCache {
    status: ScanStatus::Idle,
    aps: heapless::Vec::new(),
});
pub static WIFI_PENDING: Mutex<Cs, WlanConfig> = Mutex::new(WlanConfig {
    ssid: heapless::String::new(),
    pass: heapless::String::new(),
    mode: IpMode::Dhcp,
    ip: heapless::String::new(),
    gateway: heapless::String::new(),
    netmask: heapless::String::new(),
    dns1: heapless::String::new(),
    dns2: heapless::String::new(),
    ntp1: heapless::String::new(),
    ntp2: heapless::String::new(),
});
pub static WIFI_TEST_STATE: Mutex<Cs, heapless::String<12>> = Mutex::new(heapless::String::new());
pub static WIFI_TEST_SSID: Mutex<Cs, heapless::String<33>> = Mutex::new(heapless::String::new());
pub static WIFI_TEST_OWNS: Mutex<Cs, bool> = Mutex::new(false);
pub static STA_SSID: Mutex<Cs, heapless::String<33>> = Mutex::new(heapless::String::new());
pub static STA_RSSI: Mutex<Cs, i32> = Mutex::new(0);

