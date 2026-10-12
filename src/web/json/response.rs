//! OpenAPI-aligned JSON builders for the admin SPA.

use alloc::string::String;
use crate::audio::AudioConfig;
use crate::config::{DEVICE_HOSTNAME, SETUP_AP_IP, SETUP_AP_SSID, STA_HOSTNAME_PREFIX, UiPrefs};
use crate::ota::client::OtaStatus;
use crate::mqtt::MqttConfig;
use crate::config::APP_VERSION;
use crate::wifi::WlanConfig;

pub fn json_ok() -> &'static str {
    "{\"ok\":true}"
}

pub fn json_err(error: &str) -> String {
    let mut s = String::new();
    s.push_str("{\"ok\":false,\"error\":\"");
    push_escaped(&mut s, error);
    s.push_str("\"}");
    s
}

pub fn json_ok_next(next: &str) -> String {
    let mut s = String::new();
    s.push_str("{\"ok\":true,\"next\":\"");
    push_escaped(&mut s, next);
    s.push_str("\"}");
    s
}

pub fn response(status_line: &str, content_type: &str, body: &str) -> String {
    let mut s = String::new();
    s.push_str("HTTP/1.0 ");
    s.push_str(status_line);
    s.push_str("\r\nContent-Type: ");
    s.push_str(content_type);
    s.push_str("\r\nConnection: close\r\n\r\n");
    s.push_str(body);
    s
}

pub fn response_json(code: u16, body: &str) -> String {
    let line = match code {
        200 => "200 OK",
        202 => "202 Accepted",
        400 => "400 Bad Request",
        409 => "409 Conflict",
        503 => "503 Service Unavailable",
        _ => "500 Internal Server Error",
    };
    response(line, "application/json", body)
}

/// SSE `device` payload (C++ `fillDeviceBatteryJson`) — battery only.
pub fn device_battery_json(bat_mv: i32, bat_pct: i32) -> String {
    let mut s = String::new();
    s.push_str("{\"batteryMv\":");
    push_i32(&mut s, bat_mv);
    s.push_str(",\"batteryPct\":");
    push_i32(&mut s, bat_pct.max(0).min(100));
    s.push('}');
    s
}

pub fn device_json(
    device_id: &str,
    ap_mode: bool,
    bat_mv: i32,
    bat_pct: i32,
) -> String {
    let mut s = String::new();
    s.push('{');
    s.push_str("\"hostname\":\"");
    if ap_mode {
        push_escaped(&mut s, DEVICE_HOSTNAME);
    } else {
        push_escaped(&mut s, STA_HOSTNAME_PREFIX);
        push_escaped(&mut s, device_id);
    }
    s.push_str("\",\"version\":\"");
    push_escaped(&mut s, APP_VERSION);
    s.push_str("\",\"mode\":\"");
    s.push_str(if ap_mode { "ap" } else { "sta" });
    s.push_str("\",\"deviceId\":\"");
    push_escaped(&mut s, device_id);
    s.push_str("\",\"batteryMv\":");
    push_i32(&mut s, bat_mv);
    s.push_str(",\"batteryPct\":");
    push_i32(&mut s, bat_pct.max(0).min(100));
    if ap_mode {
        s.push_str(",\"apSsid\":\"");
        push_escaped(&mut s, SETUP_AP_SSID);
        s.push_str("\",\"apIp\":\"");
        push_escaped(&mut s, SETUP_AP_IP);
        s.push('"');
    }
    s.push('}');
    s
}

pub fn wifi_status_disconnected() -> &'static str {
    "{\"connected\":false}"
}

pub fn wifi_status_connected(
    ssid: &str,
    ip: &str,
    gateway: &str,
    netmask: &str,
    dns1: &str,
    dns2: &str,
    rssi: i32,
) -> String {
    let mut s = String::new();
    s.push_str("{\"connected\":true,\"ssid\":\"");
    push_escaped(&mut s, ssid);
    s.push_str("\",\"ip\":\"");
    push_escaped(&mut s, ip);
    s.push_str("\",\"gateway\":\"");
    push_escaped(&mut s, gateway);
    s.push_str("\",\"netmask\":\"");
    push_escaped(&mut s, netmask);
    s.push_str("\",\"dns1\":\"");
    push_escaped(&mut s, dns1);
    s.push_str("\",\"dns2\":\"");
    push_escaped(&mut s, dns2);
    s.push_str("\",\"rssi\":");
    push_i32(&mut s, rssi);
    s.push('}');
    s
}

pub fn chaya_json(rx: i32, tx: i32, connected: bool, configured: bool, paired: bool) -> String {
    let mut s = String::new();
    s.push_str("{\"rx\":");
    push_i32(&mut s, rx);
    s.push_str(",\"tx\":");
    push_i32(&mut s, tx);
    s.push_str(",\"connected\":");
    s.push_str(if connected { "true" } else { "false" });
    s.push_str(",\"configured\":");
    s.push_str(if configured { "true" } else { "false" });
    s.push_str(",\"paired\":");
    s.push_str(if paired { "true" } else { "false" });
    s.push('}');
    s
}

pub fn mqtt_status_json(connected: bool) -> String {
    let mut s = String::new();
    s.push_str("{\"connected\":");
    s.push_str(if connected { "true" } else { "false" });
    s.push('}');
    s
}

pub fn mqtt_config_json(device_id: &str, cfg: &MqttConfig) -> String {
    let pub_topic = cfg.pub_topic(device_id);
    let sub_topic = cfg.sub_topic();
    let mut s = String::new();
    s.push_str("{\"deviceId\":\"");
    push_escaped(&mut s, device_id);
    s.push_str("\",\"server\":\"");
    push_escaped(&mut s, cfg.host.as_str());
    s.push_str("\",\"port\":");
    push_i32(&mut s, cfg.port as i32);
    s.push_str(",\"tls\":");
    s.push_str(if cfg.port >= 8883 { "true" } else { "false" });
    s.push_str(",\"username\":\"");
    push_escaped(&mut s, cfg.user.as_str());
    s.push_str("\",\"hasPassword\":");
    s.push_str(if cfg.pass.is_empty() { "false" } else { "true" });
    s.push_str(",\"topicPub\":\"");
    push_escaped(&mut s, pub_topic.as_str());
    s.push_str("\",\"topicSub\":\"");
    push_escaped(&mut s, sub_topic.as_str());
    s.push_str("\",\"partnerId\":\"");
    push_escaped(&mut s, cfg.partner_id.as_str());
    s.push_str("\",\"pairingId\":\"");
    push_escaped(&mut s, cfg.pairing_id.as_str());
    s.push_str("\",\"nvsOk\":true,\"applyPending\":false}");
    s
}

pub fn settings_json(audio: &AudioConfig, ui: &UiPrefs) -> String {
    let mut s = String::new();
    s.push_str("{\"resetDays\":");
    push_i32(&mut s, ui.reset_days as i32);
    s.push_str(",\"lang\":\"");
    push_escaped(&mut s, ui.lang.as_str());
    s.push_str("\",\"theme\":\"");
    push_escaped(&mut s, ui.theme.as_str());
    s.push_str("\",\"ledEnabled\":");
    s.push_str(if ui.led_enabled { "true" } else { "false" });
    s.push_str(",\"audioTxEnabled\":");
    s.push_str(if audio.tx_enabled { "true" } else { "false" });
    s.push_str(",\"audioRxEnabled\":");
    s.push_str(if audio.rx_enabled { "true" } else { "false" });
    s.push_str(",\"audioTxVolume\":");
    push_i32(&mut s, audio.volume as i32);
    s.push_str(",\"audioRxVolume\":");
    push_i32(&mut s, audio.volume as i32);
    s.push_str(",\"quietHourStart\":");
    push_i32(&mut s, audio.quiet_start as i32);
    s.push_str(",\"quietHourEnd\":");
    push_i32(&mut s, audio.quiet_end as i32);
    s.push_str(",\"txHz\":880,\"txMs\":80,\"rxHz\":660,\"rxMs\":80,\"nvsOk\":true,\"applyPending\":false}");
    s
}

pub fn ota_status_json(st: &OtaStatus) -> String {
    let phase = if st.installing {
        "downloading"
    } else if st.checking {
        "checking"
    } else if st.flash_ready {
        "available"
    } else if !st.last_error.is_empty() {
        "error"
    } else {
        "idle"
    };
    let channel = if st.channel.is_empty() {
        "stable"
    } else {
        st.channel.as_str()
    };
    let mut s = String::new();
    s.push_str("{\"phase\":\"");
    s.push_str(phase);
    s.push_str("\",\"channel\":\"");
    push_escaped(&mut s, channel);
    s.push_str("\",\"localVersion\":\"");
    push_escaped(&mut s, APP_VERSION);
    s.push_str("\",\"availableVersion\":\"");
    push_escaped(&mut s, st.last_tag.as_str());
    s.push_str("\",\"bytesDone\":0,\"bytesTotal\":0,\"error\":\"");
    push_escaped(&mut s, st.last_error.as_str());
    s.push_str("\",\"generation\":0}");
    s
}

pub fn bootstrap_json(
    device_id: &str,
    ap_mode: bool,
    bat_mv: i32,
    bat_pct: i32,
    wifi_json: &str,
    mqtt_cfg: Option<&MqttConfig>,
    mqtt_ok: bool,
    heart_tx: i32,
    heart_rx: i32,
    audio: &AudioConfig,
    ui: &UiPrefs,
    ota: &OtaStatus,
) -> String {
    let mut s = String::new();
    s.push_str("{\"device\":");
    s.push_str(&device_json(device_id, ap_mode, bat_mv, bat_pct));
    s.push_str(",\"wifi\":");
    s.push_str(wifi_json);
    if ap_mode {
        s.push_str(",\"chaya\":null,\"mqtt\":null,\"update\":null,\"settings\":null}");
        return s;
    }
    let configured = mqtt_cfg.map(|c| c.broker_configured()).unwrap_or(false);
    let paired = mqtt_cfg.map(|c| c.partner_id.len() == 6).unwrap_or(false);
    s.push_str(",\"chaya\":");
    s.push_str(&chaya_json(heart_rx, heart_tx, mqtt_ok, configured, paired));
    s.push_str(",\"mqtt\":");
    s.push_str(&mqtt_status_json(mqtt_ok));
    s.push_str(",\"update\":");
    s.push_str(&ota_status_json(ota));
    s.push_str(",\"settings\":");
    s.push_str(&settings_json(audio, ui));
    s.push('}');
    s
}

pub fn connect_status_json(state: &str, ssid: &str) -> String {
    let mut s = String::new();
    s.push_str("{\"state\":\"");
    push_escaped(&mut s, state);
    s.push_str("\",\"ssid\":\"");
    push_escaped(&mut s, ssid);
    s.push_str("\"}");
    s
}

pub fn sse_typed(event: &str, data_json: &str) -> String {
    let mut s = String::new();
    s.push_str("event: ");
    s.push_str(event);
    s.push_str("\ndata: ");
    s.push_str(data_json);
    s.push_str("\n\n");
    s
}

pub fn parse_wlan_connect(body: &[u8], cfg: &mut WlanConfig, password_present: &mut bool) -> Result<(), &'static str> {
    use crate::web::form::form_value;
    use super::dto::{from_slice, WifiConnectReq};

    if let Ok(req) = from_slice::<WifiConnectReq>(body) {
        let ssid = req.ssid.filter(|s| !s.is_empty()).ok_or("ssid")?;
        if ssid.len() > 32 {
            return Err("ssid");
        }
        cfg.ssid.clear();
        let _ = cfg.ssid.push_str(ssid);

        *password_present = req.password.is_some();
        if let Some(p) = req.password {
            cfg.pass.clear();
            let _ = cfg.pass.push_str(p);
        }
        if let Some(m) = req.mode {
            cfg.mode = crate::wifi::IpMode::parse(m);
        }
        push_opt(&mut cfg.ip, req.ip);
        push_opt(&mut cfg.gateway, req.gateway);
        push_opt(&mut cfg.netmask, req.netmask);
        push_opt(&mut cfg.dns1, req.dns1);
        push_opt(&mut cfg.dns2, req.dns2);
        push_opt(&mut cfg.ntp1, req.ntp1);
        push_opt(&mut cfg.ntp2, req.ntp2);
        return Ok(());
    }

    // Legacy form body fallback.
    let ssid = form_value(body, "ssid").ok_or("ssid")?;
    if ssid.is_empty() || ssid.len() > 32 {
        return Err("ssid");
    }
    cfg.ssid.clear();
    let _ = cfg.ssid.push_str(ssid);

    *password_present = form_value(body, "password").is_some() || form_value(body, "pass").is_some();
    if let Some(p) = form_value(body, "password").or_else(|| form_value(body, "pass")) {
        cfg.pass.clear();
        let _ = cfg.pass.push_str(p);
    }
    if let Some(m) = form_value(body, "mode") {
        cfg.mode = crate::wifi::IpMode::parse(m);
    }
    if let Some(v) = form_value(body, "ip") {
        push_opt(&mut cfg.ip, Some(v));
    }
    if let Some(v) = form_value(body, "gateway") {
        push_opt(&mut cfg.gateway, Some(v));
    }
    if let Some(v) = form_value(body, "netmask") {
        push_opt(&mut cfg.netmask, Some(v));
    }
    if let Some(v) = form_value(body, "dns1") {
        push_opt(&mut cfg.dns1, Some(v));
    }
    if let Some(v) = form_value(body, "dns2") {
        push_opt(&mut cfg.dns2, Some(v));
    }
    if let Some(v) = form_value(body, "ntp1") {
        push_opt(&mut cfg.ntp1, Some(v));
    }
    if let Some(v) = form_value(body, "ntp2") {
        push_opt(&mut cfg.ntp2, Some(v));
    }
    Ok(())
}

fn push_opt<const N: usize>(dst: &mut heapless::String<N>, v: Option<&str>) {
    if let Some(s) = v {
        dst.clear();
        let _ = dst.push_str(s);
    }
}

fn push_escaped(out: &mut String, input: &str) {
    for c in input.chars() {
        match c {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            c if c < ' ' => {}
            c => out.push(c),
        }
    }
}

fn push_i32(out: &mut String, mut n: i32) {
    if n < 0 {
        out.push('-');
        n = -n;
    }
    if n == 0 {
        out.push('0');
        return;
    }
    let mut digs = heapless::Vec::<u8, 16>::new();
    while n > 0 {
        let _ = digs.push(b'0' + (n % 10) as u8);
        n /= 10;
    }
    for d in digs.iter().rev() {
        out.push(*d as char);
    }
}
