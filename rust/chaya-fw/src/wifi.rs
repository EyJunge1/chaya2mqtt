//! Wi-Fi STA / SoftAP orchestration (esp-radio backend pending).

use crate::config::{NS_CFG, NS_WIFI, SETUP_AP_SSID};
use crate::platform::KvStore;
use chaya_pure::constants::{WIFI_PASS_MAX_LEN, WIFI_SSID_MAX_LEN};
use chaya_pure::wifi::{wifi_qr_build_wpa_payload, WIFI_QR_PAYLOAD_MAX_LEN};

const KEY_SSID: &str = "ssid";
const KEY_PASS: &str = "pass";
const KEY_AP_PSK: &str = "ap_psk";
const AP_PSK_LEN: usize = 20;

#[derive(Clone, Debug)]
pub struct WlanConfig {
    pub ssid: heapless::String<WIFI_SSID_MAX_LEN>,
    pub pass: heapless::String<WIFI_PASS_MAX_LEN>,
}

impl Default for WlanConfig {
    fn default() -> Self {
        Self {
            ssid: heapless::String::new(),
            pass: heapless::String::new(),
        }
    }
}

impl WlanConfig {
    pub fn load(kv: &mut dyn KvStore) -> Self {
        let mut ssid_buf = [0u8; WIFI_SSID_MAX_LEN];
        let mut pass_buf = [0u8; WIFI_PASS_MAX_LEN];
        let mut cfg = Self::default();
        if let Some(n) = kv.get_blob(NS_WIFI, KEY_SSID, &mut ssid_buf) {
            let _ = cfg.ssid.push_str(core::str::from_utf8(&ssid_buf[..n]).unwrap_or(""));
        }
        if let Some(n) = kv.get_blob(NS_WIFI, KEY_PASS, &mut pass_buf) {
            let _ = cfg.pass.push_str(core::str::from_utf8(&pass_buf[..n]).unwrap_or(""));
        }
        cfg
    }

    pub fn save(&self, kv: &mut dyn KvStore) -> bool {
        kv.put_blob(NS_WIFI, KEY_SSID, self.ssid.as_bytes())
            && kv.put_blob(NS_WIFI, KEY_PASS, self.pass.as_bytes())
    }

    pub fn has_sta(&self) -> bool {
        !self.ssid.is_empty()
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum WlanMode {
    Sta,
    SoftApSetup,
}

#[derive(Clone, Debug)]
pub struct WlanService {
    pub mode: WlanMode,
    pub cfg: WlanConfig,
    pub connected: bool,
}

impl WlanService {
    pub fn boot_from_nvs(kv: &mut dyn KvStore) -> Self {
        let cfg = WlanConfig::load(kv);
        if cfg.has_sta() {
            Self {
                mode: WlanMode::Sta,
                cfg,
                connected: false,
            }
        } else {
            Self {
                mode: WlanMode::SoftApSetup,
                cfg,
                connected: false,
            }
        }
    }

    pub fn arm_setup_ap(&mut self) {
        self.mode = WlanMode::SoftApSetup;
        self.connected = false;
    }

    pub fn setup_ap_ssid() -> &'static str {
        SETUP_AP_SSID
    }

    /// SoftAP WIFI QR payload for splash (PSK must be supplied by caller when known).
    pub fn build_setup_qr(ssid: &str, pass: &str, out: &mut [u8]) -> bool {
        wifi_qr_build_wpa_payload(ssid.as_bytes(), pass.as_bytes(), out)
    }

    pub fn qr_buf_len() -> usize {
        WIFI_QR_PAYLOAD_MAX_LEN
    }

    /// Load SoftAP WPA PSK from NVS or create a 20-char alphanumeric one.
    pub fn load_or_create_ap_psk(kv: &mut dyn KvStore, entropy: [u8; 20]) -> heapless::String<32> {
        let mut buf = [0u8; 32];
        if let Some(n) = kv.get_blob(NS_CFG, KEY_AP_PSK, &mut buf) {
            if n >= 8 {
                let mut s = heapless::String::new();
                let _ = s.push_str(core::str::from_utf8(&buf[..n]).unwrap_or(""));
                if !s.is_empty() {
                    return s;
                }
            }
        }
        const ALPH: &[u8] = b"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
        let mut s = heapless::String::<32>::new();
        for i in 0..AP_PSK_LEN {
            let idx = (entropy[i] as usize) % ALPH.len();
            let _ = s.push(ALPH[idx] as char);
        }
        let _ = kv.put_blob(NS_CFG, KEY_AP_PSK, s.as_bytes());
        s
    }
}

pub fn radio_backend_status() -> &'static str {
    "device: SoftAP + STA + captive DNS (edge-captive)"
}
