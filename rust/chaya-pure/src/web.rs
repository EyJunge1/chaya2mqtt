//! From `src/web/*_pure.h`.

use crate::constants::{IPV4_STR_MAX_LEN, SSE_ALL, SSE_DEVICE, SSE_WIFI, WIFI_SSID_MAX_LEN};

pub fn web_admin_restart_blocked(
    ota_busy: bool,
    mqtt_apply_pending: bool,
    settings_apply_pending: bool,
    mqtt_apply_unqueued: bool,
    apply_in_flight: bool,
) -> bool {
    ota_busy || mqtt_apply_pending || settings_apply_pending || mqtt_apply_unqueued || apply_in_flight
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

pub fn web_admin_deferred_apply_allowed(shutdown: bool, ota_busy: bool, factory_queued: bool) -> bool {
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
        assert_eq!(
            sse_tick_mask_for_ap_mode(all, true),
            SSE_WIFI | SSE_DEVICE
        );
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
