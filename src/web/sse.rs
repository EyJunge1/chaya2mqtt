//! Captive-portal probes and compact `/api/status` JSON.

use crate::config::APP_VERSION;
use crate::config::SETUP_AP_IP;

pub fn is_captive_probe(path: &str) -> bool {
    matches!(
        path,
        "/generate_204"
            | "/gen_204"
            | "/hotspot-detect.html"
            | "/library/test/success.html"
            | "/canonical.html"
            | "/ncsi.txt"
            | "/connecttest.txt"
            | "/redirect"
    )
}

pub fn json_status(
    device_id: &str,
    ap_mode: bool,
    wifi_ok: bool,
    mqtt_ok: bool,
    heart_tx: i32,
    heart_rx: i32,
    bat_pct: i32,
    bat_mv: i32,
) -> heapless::String<320> {
    let mut s = heapless::String::new();
    let _ = s.push_str("{\"version\":\"");
    let _ = s.push_str(APP_VERSION);
    let _ = s.push_str("\",\"id\":\"");
    let _ = s.push_str(device_id);
    let _ = s.push_str("\",\"ap\":");
    let _ = s.push_str(if ap_mode { "true" } else { "false" });
    let _ = s.push_str(",\"wifi\":");
    let _ = s.push_str(if wifi_ok { "true" } else { "false" });
    let _ = s.push_str(",\"mqtt\":");
    let _ = s.push_str(if mqtt_ok { "true" } else { "false" });
    let _ = s.push_str(",\"heartTx\":");
    append_i32(&mut s, heart_tx);
    let _ = s.push_str(",\"heartRx\":");
    append_i32(&mut s, heart_rx);
    let _ = s.push_str(",\"batteryPct\":");
    append_i32(&mut s, bat_pct);
    let _ = s.push_str(",\"batteryMv\":");
    append_i32(&mut s, bat_mv);
    let _ = s.push('}');
    s
}

fn append_i32(s: &mut heapless::String<320>, n: i32) {
    let mut buf = itoa::Buffer::new();
    let _ = s.push_str(buf.format(n));
}

pub fn sse_keepalive() -> &'static [u8] {
    b": keepalive\n\n"
}

/// Absolute SoftAP landing URL (Android/Windows prefer absolute).
pub fn captive_landing() -> &'static str {
    let _ = SETUP_AP_IP;
    "http://4.3.2.1/"
}
