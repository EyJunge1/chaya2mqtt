//! Wi-Fi STA / SoftAP orchestration (esp-radio backend).

use core::net::Ipv4Addr;
use core::str::FromStr;

use crate::config::{NS_CFG, NS_WIFI, SETUP_AP_SSID};
use crate::core::constants::{WIFI_PASS_MAX_LEN, WIFI_SSID_MAX_LEN};
use crate::core::wifi::{wifi_qr_build_wpa_payload, WIFI_QR_PAYLOAD_MAX_LEN};
use crate::util::platform::KvStore;

const KEY_SSID: &str = "ssid";
const KEY_PASS: &str = "pass";
const KEY_MODE: &str = "ipmode";
const KEY_IP: &str = "ip";
const KEY_GW: &str = "gw";
const KEY_MASK: &str = "mask";
const KEY_DNS1: &str = "dns1";
const KEY_DNS2: &str = "dns2";
const KEY_NTP1: &str = "ntp1";
const KEY_NTP2: &str = "ntp2";
const KEY_AP_PSK: &str = "ap_psk";
const AP_PSK_LEN: usize = 20;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum IpMode {
    Dhcp,
    Static,
}

impl IpMode {
    pub fn as_str(self) -> &'static str {
        match self {
            Self::Dhcp => "dhcp",
            Self::Static => "static",
        }
    }

    pub fn parse(s: &str) -> Self {
        if s.eq_ignore_ascii_case("static") {
            Self::Static
        } else {
            Self::Dhcp
        }
    }
}

#[derive(Clone, Debug)]
pub struct WlanConfig {
    pub ssid: heapless::String<WIFI_SSID_MAX_LEN>,
    pub pass: heapless::String<WIFI_PASS_MAX_LEN>,
    pub mode: IpMode,
    pub ip: heapless::String<16>,
    pub gateway: heapless::String<16>,
    pub netmask: heapless::String<16>,
    pub dns1: heapless::String<16>,
    pub dns2: heapless::String<16>,
    pub ntp1: heapless::String<64>,
    pub ntp2: heapless::String<64>,
}

impl Default for WlanConfig {
    fn default() -> Self {
        Self {
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
        }
    }
}

fn load_str<const N: usize>(kv: &mut dyn KvStore, key: &str, out: &mut heapless::String<N>) {
    let mut buf = [0u8; 64];
    if let Some(n) = kv.get_blob(NS_WIFI, key, &mut buf) {
        let _ = out.push_str(core::str::from_utf8(&buf[..n.min(N)]).unwrap_or(""));
    }
}

impl WlanConfig {
    pub fn load(kv: &mut dyn KvStore) -> Self {
        let mut cfg = Self::default();
        load_str(kv, KEY_SSID, &mut cfg.ssid);
        load_str(kv, KEY_PASS, &mut cfg.pass);
        let mut mode_buf = [0u8; 8];
        if let Some(n) = kv.get_blob(NS_WIFI, KEY_MODE, &mut mode_buf) {
            cfg.mode = IpMode::parse(core::str::from_utf8(&mode_buf[..n]).unwrap_or("dhcp"));
        }
        load_str(kv, KEY_IP, &mut cfg.ip);
        load_str(kv, KEY_GW, &mut cfg.gateway);
        load_str(kv, KEY_MASK, &mut cfg.netmask);
        load_str(kv, KEY_DNS1, &mut cfg.dns1);
        load_str(kv, KEY_DNS2, &mut cfg.dns2);
        load_str(kv, KEY_NTP1, &mut cfg.ntp1);
        load_str(kv, KEY_NTP2, &mut cfg.ntp2);
        cfg
    }

    pub fn save(&self, kv: &mut dyn KvStore) -> bool {
        kv.put_blob(NS_WIFI, KEY_SSID, self.ssid.as_bytes())
            && kv.put_blob(NS_WIFI, KEY_PASS, self.pass.as_bytes())
            && kv.put_blob(NS_WIFI, KEY_MODE, self.mode.as_str().as_bytes())
            && kv.put_blob(NS_WIFI, KEY_IP, self.ip.as_bytes())
            && kv.put_blob(NS_WIFI, KEY_GW, self.gateway.as_bytes())
            && kv.put_blob(NS_WIFI, KEY_MASK, self.netmask.as_bytes())
            && kv.put_blob(NS_WIFI, KEY_DNS1, self.dns1.as_bytes())
            && kv.put_blob(NS_WIFI, KEY_DNS2, self.dns2.as_bytes())
            && kv.put_blob(NS_WIFI, KEY_NTP1, self.ntp1.as_bytes())
            && kv.put_blob(NS_WIFI, KEY_NTP2, self.ntp2.as_bytes())
    }

    pub fn has_sta(&self) -> bool {
        !self.ssid.is_empty()
    }

    /// Parsed static IPv4 fields for embassy-net. `None` when mode is DHCP or values are invalid.
    pub fn static_v4(&self) -> Option<StaticV4Params> {
        if self.mode != IpMode::Static {
            return None;
        }
        let ip = parse_ipv4(self.ip.as_str())?;
        let gateway = parse_ipv4(self.gateway.as_str())?;
        let prefix = if self.netmask.is_empty() {
            24
        } else {
            netmask_prefix_len(parse_ipv4(self.netmask.as_str())?)?
        };
        Some(StaticV4Params {
            address: ip,
            prefix,
            gateway,
            dns1: parse_ipv4(self.dns1.as_str()),
            dns2: parse_ipv4(self.dns2.as_str()),
        })
    }

    /// OpenAPI `WifiConfig` (no password).
    pub fn to_config_json(&self) -> heapless::String<384> {
        let mut s = heapless::String::new();
        let _ = s.push_str("{\"ssid\":\"");
        push_json_escaped(&mut s, self.ssid.as_str());
        let _ = s.push_str("\",\"mode\":\"");
        let _ = s.push_str(self.mode.as_str());
        let _ = s.push_str("\",\"ip\":\"");
        push_json_escaped(&mut s, self.ip.as_str());
        let _ = s.push_str("\",\"gateway\":\"");
        push_json_escaped(&mut s, self.gateway.as_str());
        let _ = s.push_str("\",\"netmask\":\"");
        push_json_escaped(&mut s, self.netmask.as_str());
        let _ = s.push_str("\",\"dns1\":\"");
        push_json_escaped(&mut s, self.dns1.as_str());
        let _ = s.push_str("\",\"dns2\":\"");
        push_json_escaped(&mut s, self.dns2.as_str());
        let _ = s.push_str("\",\"ntp1\":\"");
        push_json_escaped(&mut s, self.ntp1.as_str());
        let _ = s.push_str("\",\"ntp2\":\"");
        push_json_escaped(&mut s, self.ntp2.as_str());
        let _ = s.push_str("\"}");
        s
    }
}

/// Static IPv4 parameters ready for `embassy_net::StaticConfigV4`.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct StaticV4Params {
    pub address: Ipv4Addr,
    pub prefix: u8,
    pub gateway: Ipv4Addr,
    pub dns1: Option<Ipv4Addr>,
    pub dns2: Option<Ipv4Addr>,
}

pub fn parse_ipv4(s: &str) -> Option<Ipv4Addr> {
    let s = s.trim();
    if s.is_empty() {
        return None;
    }
    Ipv4Addr::from_str(s).ok()
}

/// Contiguous netmask → CIDR prefix length (`255.255.255.0` → 24).
pub fn netmask_prefix_len(mask: Ipv4Addr) -> Option<u8> {
    let bits = u32::from(mask);
    let n = bits.leading_ones();
    if bits == 0 || (bits << n) == 0 {
        Some(n as u8)
    } else {
        None
    }
}

fn push_json_escaped(out: &mut heapless::String<384>, input: &str) {
    for c in input.chars() {
        match c {
            '"' => {
                let _ = out.push_str("\\\"");
            }
            '\\' => {
                let _ = out.push_str("\\\\");
            }
            c if c < ' ' => {}
            c => {
                let _ = out.push(c);
            }
        }
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
    "device: SoftAP+STA (APSTA) + captive DNS + scan/connect API"
}

#[cfg(feature = "device")]
pub mod ops;
#[cfg(feature = "device")]
pub mod scan;

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn netmask_prefix_common() {
        assert_eq!(
            netmask_prefix_len(Ipv4Addr::new(255, 255, 255, 0)),
            Some(24)
        );
        assert_eq!(netmask_prefix_len(Ipv4Addr::new(255, 255, 0, 0)), Some(16));
        assert_eq!(netmask_prefix_len(Ipv4Addr::new(255, 0, 0, 0)), Some(8));
        assert_eq!(netmask_prefix_len(Ipv4Addr::new(255, 0, 255, 0)), None);
    }

    #[test]
    fn static_v4_requires_valid_fields() {
        let mut cfg = WlanConfig::default();
        cfg.mode = IpMode::Static;
        assert!(cfg.static_v4().is_none());
        cfg.ip = heapless::String::try_from("192.168.1.50").unwrap();
        cfg.gateway = heapless::String::try_from("192.168.1.1").unwrap();
        cfg.netmask = heapless::String::try_from("255.255.255.0").unwrap();
        cfg.dns1 = heapless::String::try_from("1.1.1.1").unwrap();
        let p = cfg.static_v4().unwrap();
        assert_eq!(p.address, Ipv4Addr::new(192, 168, 1, 50));
        assert_eq!(p.prefix, 24);
        assert_eq!(p.gateway, Ipv4Addr::new(192, 168, 1, 1));
        assert_eq!(p.dns1, Some(Ipv4Addr::new(1, 1, 1, 1)));
    }
}
