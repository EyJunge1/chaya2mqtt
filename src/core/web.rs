//! From `src/web/*_pure.h` and `src/web/host_validate.h`.

use super::constants::{IPV4_STR_MAX_LEN, SSE_ALL, SSE_DEVICE, SSE_WIFI, WIFI_SSID_MAX_LEN};

/// Case-insensitive full-string host equality (C++ `hostEqualsIgnoreCase`).
pub fn host_equals_ignore_case(host: &str, reference: &str) -> bool {
    host.eq_ignore_ascii_case(reference)
}

/// True when `host` equals `prefix` (case-insensitive) or `prefix` followed by `:port`
/// (C++ `hostPrefixIgnoreCaseThenPortOrEnd`).
pub fn host_prefix_ignore_case_then_port_or_end(host: &str, prefix: &str) -> bool {
    let hb = host.as_bytes();
    let pb = prefix.as_bytes();
    if hb.len() < pb.len() {
        return false;
    }
    if !hb[..pb.len()].eq_ignore_ascii_case(pb) {
        return false;
    }
    hb.len() == pb.len() || hb[pb.len()] == b':'
}

/// `{hostname}.local` or `{hostname}.local:port` without allocating
/// (C++ `formatToBuf("{}.local")` + `hostPrefixIgnoreCaseThenPortOrEnd`).
fn host_prefix_hostname_local_then_port_or_end(host: &str, hostname: &str) -> bool {
    const LOCAL: &[u8] = b".local";
    let need = hostname.len() + LOCAL.len();
    let hb = host.as_bytes();
    if hb.len() < need {
        return false;
    }
    if !hb[..hostname.len()].eq_ignore_ascii_case(hostname.as_bytes()) {
        return false;
    }
    if !hb[hostname.len()..need].eq_ignore_ascii_case(LOCAL) {
        return false;
    }
    hb.len() == need || hb[need] == b':'
}

/// Host allow-list (C++ `webHostCStringAllowed`).
///
/// - `host`: request Host (may include `:port`); empty = missing (HTTP/1.0)
/// - `ap_mode`: SoftAP captive portal allowlist
/// - `device_hostname`: STA hostname without `.local`; empty rejects in STA
/// - `sta_ip`: optional STA IPv4 string; `None`/empty skips IP match
pub fn web_host_allowed(
    host: &str,
    ap_mode: bool,
    device_hostname: &str,
    sta_ip: Option<&str>,
) -> bool {
    if host.is_empty() {
        // HTTP/1.1 requires Host. Keep hostless HTTP/1.0 captive probes working only in AP mode.
        return ap_mode;
    }
    if ap_mode {
        // SEC-04: SoftAP allowlist only — setup IP / captive hostname (not arbitrary Host).
        if host_equals_ignore_case(host, "4.3.2.1") {
            return true;
        }
        if host_prefix_ignore_case_then_port_or_end(host, "4.3.2.1") {
            return true;
        }
        if host_prefix_ignore_case_then_port_or_end(host, "chaya2mqtt") {
            return true;
        }
        if host_prefix_ignore_case_then_port_or_end(host, "chaya2mqtt.local") {
            return true;
        }
        return false;
    }
    if device_hostname.is_empty() {
        return false;
    }
    if host_prefix_ignore_case_then_port_or_end(host, device_hostname) {
        return true;
    }
    if host_prefix_hostname_local_then_port_or_end(host, device_hostname) {
        return true;
    }
    if let Some(ip) = sta_ip {
        if !ip.is_empty() {
            if host_equals_ignore_case(host, ip) {
                return true;
            }
            let hb = host.as_bytes();
            let ib = ip.as_bytes();
            // C++ uses case-sensitive `starts_with` for the `:port` IP form.
            if hb.len() > ib.len() && hb[..ib.len()] == *ib && hb[ib.len()] == b':' {
                return true;
            }
        }
    }
    false
}

pub fn web_admin_restart_blocked(
    ota_busy: bool,
    mqtt_apply_pending: bool,
    settings_apply_pending: bool,
    mqtt_apply_unqueued: bool,
    apply_in_flight: bool,
) -> bool {
    ota_busy
        || mqtt_apply_pending
        || settings_apply_pending
        || mqtt_apply_unqueued
        || apply_in_flight
}

pub fn web_admin_apply_commit_allowed(shutdown: bool, armed: bool, factory_queued: bool) -> bool {
    armed && !shutdown && !factory_queued
}

pub fn admin_apply_blocked_by_ota(ota_busy: bool) -> bool {
    ota_busy
}

pub fn web_admin_ota_start_blocked(
    mqtt_apply_pending: bool,
    settings_apply_pending: bool,
    mqtt_apply_unqueued: bool,
    apply_in_flight: bool,
) -> bool {
    web_admin_restart_blocked(
        false,
        mqtt_apply_pending,
        settings_apply_pending,
        mqtt_apply_unqueued,
        apply_in_flight,
    )
}

pub fn web_admin_deferred_apply_allowed(
    shutdown: bool,
    ota_busy: bool,
    factory_queued: bool,
) -> bool {
    !shutdown && !ota_busy && !factory_queued
}

pub fn web_admin_mqtt_apply_unqueued_pure(version: u32, queued: u32) -> bool {
    version > queued
}

/// Decide which SSE domains to gather this tick (PERF-03).
/// Returns `(bits, keepalive)`.
pub fn sse_tick_select_bits(
    pending_bits: u32,
    now_ms: u32,
    last_work_ms: u32,
    keepalive_ms: u32,
) -> (u32, bool) {
    if pending_bits != 0 {
        return (pending_bits, false);
    }
    if keepalive_ms == 0 {
        return (0, false);
    }
    if last_work_ms == 0 || now_ms.wrapping_sub(last_work_ms) >= keepalive_ms {
        return (SSE_WIFI | SSE_DEVICE, true);
    }
    (0, false)
}

pub fn sse_tick_mask_for_ap_mode(work_bits: u32, ap_mode: bool) -> u32 {
    if ap_mode {
        work_bits & (SSE_WIFI | SSE_DEVICE)
    } else {
        work_bits
    }
}

pub fn sse_tick_force_snapshot(select_bits: u32) -> bool {
    (select_bits & SSE_ALL) == SSE_ALL
}

pub fn mqtt_page_conn(configured: bool, connected: bool) -> bool {
    configured && connected
}

#[derive(Clone, Debug)]
pub struct WifiStaNetFields {
    pub connected: bool,
    pub ssid: [u8; WIFI_SSID_MAX_LEN],
    pub ip: [u8; IPV4_STR_MAX_LEN],
    pub gateway: [u8; IPV4_STR_MAX_LEN],
    pub netmask: [u8; IPV4_STR_MAX_LEN],
    pub dns1: [u8; IPV4_STR_MAX_LEN],
    pub dns2: [u8; IPV4_STR_MAX_LEN],
    pub rssi: i32,
}

impl Default for WifiStaNetFields {
    fn default() -> Self {
        Self {
            connected: false,
            ssid: [0; WIFI_SSID_MAX_LEN],
            ip: [0; IPV4_STR_MAX_LEN],
            gateway: [0; IPV4_STR_MAX_LEN],
            netmask: [0; IPV4_STR_MAX_LEN],
            dns1: [0; IPV4_STR_MAX_LEN],
            dns2: [0; IPV4_STR_MAX_LEN],
            rssi: 0,
        }
    }
}

#[derive(Clone, Debug, Default)]
pub struct WifiStaNetCache {
    pub have: bool,
    pub fields: WifiStaNetFields,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum WifiStaNetResolve {
    Fresh,
    Cached,
    None,
}

pub fn wifi_sta_net_resolve_snapshot(
    snapshot_ok: bool,
    cache: &mut WifiStaNetCache,
    live: &mut WifiStaNetFields,
) -> WifiStaNetResolve {
    if snapshot_ok {
        cache.have = true;
        cache.fields = live.clone();
        return WifiStaNetResolve::Fresh;
    }
    if !cache.have {
        return WifiStaNetResolve::None;
    }
    *live = cache.fields.clone();
    WifiStaNetResolve::Cached
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn host_validate() {
        // Host-allowlist cases for `web_host_allowed` (hostname / .local / IP / AP).
        const HOSTNAME: &str = "chaya2mqtt-a1b2c3";
        const DEVICE_HOSTNAME: &str = "chaya2mqtt";

        assert!(!web_host_allowed("", false, HOSTNAME, None));
        assert!(web_host_allowed("", true, DEVICE_HOSTNAME, None));
        assert!(web_host_allowed("chaya2mqtt-a1b2c3", false, HOSTNAME, None));
        assert!(web_host_allowed(
            "chaya2mqtt-a1b2c3:80",
            false,
            HOSTNAME,
            None
        ));
        assert!(web_host_allowed(
            "Chaya2MQTT-a1b2c3.local",
            false,
            HOSTNAME,
            None
        ));
        assert!(web_host_allowed(
            "chaya2mqtt-a1b2c3.local:80",
            false,
            HOSTNAME,
            None
        ));
        assert!(web_host_allowed("4.3.2.1", true, DEVICE_HOSTNAME, None));
        assert!(web_host_allowed("chaya2mqtt", true, DEVICE_HOSTNAME, None));
        assert!(web_host_allowed(
            "chaya2mqtt:80",
            true,
            DEVICE_HOSTNAME,
            None
        ));
        assert!(web_host_allowed(
            "chaya2mqtt.local",
            true,
            DEVICE_HOSTNAME,
            None
        ));
        assert!(!web_host_allowed(
            "captive.apple.com",
            true,
            DEVICE_HOSTNAME,
            None
        ));
        assert!(!web_host_allowed(
            "evil.example",
            true,
            DEVICE_HOSTNAME,
            None
        ));
        assert!(!web_host_allowed("chaya2mqtt.local", false, HOSTNAME, None));
        assert!(!web_host_allowed("evil.example", false, HOSTNAME, None));
        assert!(web_host_allowed(
            "192.168.1.2",
            false,
            HOSTNAME,
            Some("192.168.1.2")
        ));
        assert!(web_host_allowed(
            "192.168.1.2:8080",
            false,
            HOSTNAME,
            Some("192.168.1.2")
        ));
        assert!(!web_host_allowed(
            "192.168.1.3",
            false,
            HOSTNAME,
            Some("192.168.1.2")
        ));
        assert!(!web_host_allowed("chaya2mqtt-a1b2c3", false, "", None));
    }

    #[test]
    fn sse_keepalive() {
        let (bits, ka) = sse_tick_select_bits(0, 10_000, 0, 5_000);
        assert!(ka);
        assert_eq!(bits, SSE_WIFI | SSE_DEVICE);
        let (bits2, ka2) = sse_tick_select_bits(0, 100, 50, 5_000);
        assert!(!ka2);
        assert_eq!(bits2, 0);
    }

    #[test]
    fn ap_masks_sse() {
        let all = SSE_ALL;
        assert_eq!(sse_tick_mask_for_ap_mode(all, true), SSE_WIFI | SSE_DEVICE);
    }

    #[test]
    fn cache_fallback() {
        let mut cache = WifiStaNetCache::default();
        let mut live = WifiStaNetFields {
            connected: true,
            rssi: -40,
            ..Default::default()
        };
        assert_eq!(
            wifi_sta_net_resolve_snapshot(true, &mut cache, &mut live),
            WifiStaNetResolve::Fresh
        );
        live.connected = false;
        assert_eq!(
            wifi_sta_net_resolve_snapshot(false, &mut cache, &mut live),
            WifiStaNetResolve::Cached
        );
        assert!(live.connected);
        assert_eq!(live.rssi, -40);
    }
}
