//! SoftAP/STA admin HTTP helpers (status, captive probes, SSE stub).

use crate::config::SETUP_AP_IP;
use crate::version::APP_VERSION;

/// First request line path (no query), best-effort.
pub fn request_path(req: &[u8]) -> &str {
    let Ok(s) = core::str::from_utf8(req) else {
        return "/";
    };
    let line = s.lines().next().unwrap_or("");
    let mut parts = line.split_whitespace();
    let _method = parts.next();
    let path = parts.next().unwrap_or("/");
    path.split('?').next().unwrap_or("/")
}

pub fn is_get(req: &[u8]) -> bool {
    req.starts_with(b"GET ")
}

pub fn is_post(req: &[u8]) -> bool {
    req.starts_with(b"POST ")
}

pub fn body(req: &[u8]) -> &[u8] {
    req.windows(4)
        .position(|w| w == b"\r\n\r\n")
        .map(|i| &req[i + 4..])
        .unwrap_or(&[])
}

pub fn form_value<'a>(body: &'a [u8], key: &str) -> Option<&'a str> {
    let Ok(s) = core::str::from_utf8(body) else {
        return None;
    };
    for pair in s.split('&') {
        let mut it = pair.splitn(2, '=');
        let k = it.next()?;
        let v = it.next().unwrap_or("");
        if k == key {
            return Some(percent_decode_simple(v));
        }
    }
    None
}

fn percent_decode_simple(s: &str) -> &str {
    // keep raw for ASCII ssids/pass; '+' → space is rare for wifi
    s
}

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

pub fn html_index(
    device_id: &str,
    ap_mode: bool,
    wifi_ok: bool,
    mqtt_ok: bool,
    heart_tx: i32,
    heart_rx: i32,
    bat_pct: i32,
) -> heapless::String<768> {
    let mut s = heapless::String::new();
    let _ = s.push_str("<!DOCTYPE html><html><body>");
    let _ = s.push_str("<h1>Chaya2MQTT</h1>");
    let _ = s.push_str("<p>ver ");
    let _ = s.push_str(APP_VERSION);
    let _ = s.push_str(" id ");
    let _ = s.push_str(device_id);
    let _ = s.push_str("</p>");
    if ap_mode {
        let _ = s.push_str("<p>Setup SoftAP — Wi‑Fi:</p>");
        let _ = s.push_str(
            "<form method=POST action=/api/wifi>\
             SSID <input name=ssid><br>\
             Pass <input name=pass type=password><br>\
             <button type=submit>Save Wi‑Fi</button></form>",
        );
        let _ = s.push_str("<p>MQTT (optional):</p>");
        let _ = s.push_str(
            "<form method=POST action=/api/mqtt>\
             Host <input name=host><br>\
             Port <input name=port value=1883><br>\
             User <input name=user><br>\
             Pass <input name=pass type=password><br>\
             Partner <input name=partner maxlength=6><br>\
             <button type=submit>Save MQTT</button></form>",
        );
    } else {
        let _ = s.push_str("<p>STA · <a href=/api/status>JSON</a> · <a href=/api/events>SSE</a></p>");
        let _ = s.push_str(
            "<form method=POST action=/api/send><button type=submit>Send heart</button></form>",
        );
    }
    let _ = s.push_str("<p>wifi=");
    push_bool01(&mut s, wifi_ok);
    let _ = s.push_str(" mqtt=");
    push_bool01(&mut s, mqtt_ok);
    let _ = s.push_str(" tx=");
    push_i32(&mut s, heart_tx);
    let _ = s.push_str(" rx=");
    push_i32(&mut s, heart_rx);
    let _ = s.push_str(" bat=");
    push_i32(&mut s, bat_pct);
    let _ = s.push_str("%</p></body></html>");
    s
}

fn push_bool01(s: &mut heapless::String<768>, v: bool) {
    let _ = s.push(if v { '1' } else { '0' });
}

fn push_i32(s: &mut heapless::String<768>, mut n: i32) {
    if n < 0 {
        let _ = s.push('-');
        n = -n;
    }
    if n == 0 {
        let _ = s.push('0');
        return;
    }
    let mut digs = heapless::Vec::<u8, 16>::new();
    while n > 0 {
        let _ = digs.push(b'0' + (n % 10) as u8);
        n /= 10;
    }
    for d in digs.iter().rev() {
        let _ = s.push(*d as char);
    }
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
    append_i32_small(&mut s, heart_tx);
    let _ = s.push_str(",\"heartRx\":");
    append_i32_small(&mut s, heart_rx);
    let _ = s.push_str(",\"batteryPct\":");
    append_i32_small(&mut s, bat_pct);
    let _ = s.push_str(",\"batteryMv\":");
    append_i32_small(&mut s, bat_mv);
    let _ = s.push('}');
    s
}

fn append_i32_small(s: &mut heapless::String<320>, mut n: i32) {
    if n < 0 {
        let _ = s.push('-');
        n = -n;
    }
    if n == 0 {
        let _ = s.push('0');
        return;
    }
    let mut digs = heapless::Vec::<u8, 16>::new();
    while n > 0 {
        let _ = digs.push(b'0' + (n % 10) as u8);
        n /= 10;
    }
    for d in digs.iter().rev() {
        let _ = s.push(*d as char);
    }
}

pub fn sse_event(heart_tx: i32, heart_rx: i32, mqtt_ok: bool, bat_pct: i32) -> heapless::String<896> {
    let mut body = heapless::String::<160>::new();
    let _ = body.push_str("data: {\"heartTx\":");
    let mut tmp = heapless::String::<320>::new();
    append_i32_small(&mut tmp, heart_tx);
    let _ = body.push_str(tmp.as_str());
    tmp.clear();
    let _ = body.push_str(",\"heartRx\":");
    append_i32_small(&mut tmp, heart_rx);
    let _ = body.push_str(tmp.as_str());
    tmp.clear();
    let _ = body.push_str(",\"mqtt\":");
    let _ = body.push_str(if mqtt_ok { "true" } else { "false" });
    let _ = body.push_str(",\"batteryPct\":");
    append_i32_small(&mut tmp, bat_pct);
    let _ = body.push_str(tmp.as_str());
    let _ = body.push_str("}\n\n");

    let mut s = heapless::String::new();
    let _ = s.push_str("HTTP/1.0 200 OK\r\nContent-Type: text/event-stream\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\n");
    let _ = s.push_str(body.as_str());
    s
}

pub fn response_ok(content_type: &str, body: &str) -> heapless::String<896> {
    let mut s = heapless::String::new();
    let _ = s.push_str("HTTP/1.0 200 OK\r\nContent-Type: ");
    let _ = s.push_str(content_type);
    let _ = s.push_str("\r\nConnection: close\r\n\r\n");
    let _ = s.push_str(body);
    s
}

pub fn response_redirect(loc: &str) -> heapless::String<896> {
    let mut s = heapless::String::new();
    let _ = s.push_str("HTTP/1.0 302 Found\r\nLocation: ");
    let _ = s.push_str(loc);
    let _ = s.push_str("\r\nConnection: close\r\n\r\n");
    s
}

pub fn response_empty(code: u16) -> heapless::String<896> {
    let mut s = heapless::String::new();
    let _ = s.push_str("HTTP/1.0 ");
    if code == 200 {
        let _ = s.push_str("200 OK");
    } else {
        let _ = s.push_str("404 Not Found");
    }
    let _ = s.push_str("\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
    s
}

/// Absolute SoftAP landing URL (Android/Windows prefer absolute).
pub fn captive_landing() -> &'static str {
    let _ = SETUP_AP_IP;
    "http://4.3.2.1/"
}
