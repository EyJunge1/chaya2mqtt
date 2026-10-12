//! From `src/wifi/*_pure.h`.

use super::constants::{WIFI_PASS_MAX_LEN, WIFI_SSID_MAX_LEN};

/// Max MeCard WIFI payload: prefix + escaped SSID/pass + separators + NUL.
pub const WIFI_QR_PAYLOAD_MAX_LEN: usize =
    13 + (WIFI_SSID_MAX_LEN - 1) * 2 + 3 + (WIFI_PASS_MAX_LEN - 1) * 2 + 2 + 1;

pub fn factory_wipe_should_abort(all_cleared: bool, any_mutated: bool, erased: bool) -> bool {
    !all_cleared && !any_mutated && !erased
}

pub fn factory_wipe_must_restart_unready(ready: bool, any_mutated: bool, erased: bool) -> bool {
    !ready && (any_mutated || erased)
}

pub fn wlan_net_cmd_should_enqueue(previous_pending: bool) -> bool {
    !previous_pending
}

pub fn wlan_ms_before_deadline(now_ms: u32, next_allowed_ms: u32) -> bool {
    next_allowed_ms != 0 && (now_ms.wrapping_sub(next_allowed_ms) as i32) < 0
}

pub fn wifi_scan_service_should_defer_kick(connection_test_owns_radio: bool, ota_busy: bool) -> bool {
    connection_test_owns_radio || ota_busy
}

pub fn wifi_scan_service_may_start_kick(in_progress: bool) -> bool {
    !in_progress
}

pub fn wlan_mdns_kick_should_consume(epd_active: bool, ap_mode: bool, sta_ok: bool) -> bool {
    !epd_active && !ap_mode && sta_ok
}

fn needs_escape(c: u8) -> bool {
    matches!(c, b'\\' | b';' | b',' | b'"' | b':')
}

/// Escape a WIFI MeCard field. Returns bytes written excluding NUL, or `None` on overflow.
pub fn wifi_qr_escape_field(input: &[u8], out: &mut [u8]) -> Option<usize> {
    if out.is_empty() {
        return None;
    }
    let mut o = 0usize;
    for &c in input {
        let need = if needs_escape(c) { 2 } else { 1 };
        if o + need >= out.len() {
            out[0] = 0;
            return None;
        }
        if needs_escape(c) {
            out[o] = b'\\';
            o += 1;
        }
        out[o] = c;
        o += 1;
    }
    out[o] = 0;
    Some(o)
}

/// Build `WIFI:T:WPA;S:<ssid>;P:<pass>;;` into `out` (NUL-terminated). Returns false on failure.
pub fn wifi_qr_build_wpa_payload(ssid: &[u8], pass: &[u8], out: &mut [u8]) -> bool {
    if ssid.is_empty() || out.is_empty() {
        return false;
    }
    let mut esc_ssid = [0u8; (WIFI_SSID_MAX_LEN - 1) * 2 + 1];
    let mut esc_pass = [0u8; (WIFI_PASS_MAX_LEN - 1) * 2 + 1];
    if wifi_qr_escape_field(ssid, &mut esc_ssid).is_none() {
        return false;
    }
    if !pass.is_empty() && wifi_qr_escape_field(pass, &mut esc_pass).is_none() {
        return false;
    }
    if pass.is_empty() {
        esc_pass[0] = 0;
    }

    let ssid_esc = cstr_bytes(&esc_ssid);
    let pass_esc = cstr_bytes(&esc_pass);
    let prefix = b"WIFI:T:WPA;S:";
    let mid = b";P:";
    let suffix = b";;";
    let need = prefix.len() + ssid_esc.len() + mid.len() + pass_esc.len() + suffix.len() + 1;
    if need > out.len() {
        return false;
    }
    let mut o = 0;
    for chunk in [prefix.as_slice(), ssid_esc, mid, pass_esc, suffix] {
        out[o..o + chunk.len()].copy_from_slice(chunk);
        o += chunk.len();
    }
    out[o] = 0;
    true
}

fn cstr_bytes(buf: &[u8]) -> &[u8] {
    match buf.iter().position(|&c| c == 0) {
        Some(i) => &buf[..i],
        None => buf,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn escape_specials() {
        let mut out = [0u8; 32];
        let n = wifi_qr_escape_field(b"a;b", &mut out).unwrap();
        assert_eq!(n, 4);
        assert_eq!(&out[..4], b"a\\;b");
    }

    #[test]
    fn build_payload() {
        let mut out = [0u8; WIFI_QR_PAYLOAD_MAX_LEN];
        assert!(wifi_qr_build_wpa_payload(b"home", b"secret", &mut out));
        let s = core::str::from_utf8(cstr_bytes(&out)).unwrap();
        assert_eq!(s, "WIFI:T:WPA;S:home;P:secret;;");
    }

    #[test]
    fn factory_wipe_gates() {
        assert!(factory_wipe_should_abort(false, false, false));
        assert!(factory_wipe_must_restart_unready(false, true, false));
    }
}
