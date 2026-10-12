//! Captive-portal probes and SSE keepalive framing.

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

pub fn sse_keepalive() -> &'static [u8] {
    b": keepalive\n\n"
}

/// Absolute SoftAP landing URL (Android/Windows prefer absolute).
pub fn captive_landing() -> &'static str {
    let _ = SETUP_AP_IP;
    "http://4.3.2.1/"
}
