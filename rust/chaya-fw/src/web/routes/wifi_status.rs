//! WiFi status JSON for bootstrap.

use core::fmt::Write as _;
use core::net::Ipv4Addr;

use crate::web::json;
use crate::web::shared::HttpShared;

pub async fn wifi_status_json(s: &HttpShared) -> alloc::string::String {
    if s.ap_mode {
        return alloc::string::String::from(json::wifi_status_disconnected());
    }
    let ssid = s.sta_ssid.lock().await.clone();
    let rssi = *s.sta_rssi.lock().await;
    if let Some(cfg) = s.stack.config_v4() {
        let ip = cfg.address.address();
        let gw = cfg.gateway.unwrap_or(Ipv4Addr::UNSPECIFIED);
        let dns1 = cfg.dns_servers.get(0).copied().unwrap_or(Ipv4Addr::UNSPECIFIED);
        let dns2 = cfg.dns_servers.get(1).copied().unwrap_or(Ipv4Addr::UNSPECIFIED);
        // embassy-net uses prefix len; approximate dotted netmask for /24 default.
        let mask = match cfg.address.prefix_len() {
            24 => Ipv4Addr::new(255, 255, 255, 0),
            16 => Ipv4Addr::new(255, 255, 0, 0),
            8 => Ipv4Addr::new(255, 0, 0, 0),
            _ => Ipv4Addr::new(255, 255, 255, 0),
        };
        return json::wifi_status_connected(
            ssid.as_str(),
            &ipv4_str(ip),
            &ipv4_str(gw),
            &ipv4_str(mask),
            &ipv4_str(dns1),
            &ipv4_str(dns2),
            rssi,
        );
    }
    alloc::string::String::from(json::wifi_status_disconnected())
}

fn ipv4_str(ip: Ipv4Addr) -> heapless::String<16> {
    let mut s = heapless::String::new();
    let _ = write!(&mut s, "{ip}");
    s
}

