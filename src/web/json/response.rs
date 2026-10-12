//! OpenAPI-aligned JSON builders for the admin SPA (`serde-json-core` Serialize).
//!
//! String escaping is handled by `serde-json-core` 0.6 (`serialize_str`).

use alloc::string::String;
use alloc::vec;
use serde::Serialize;

use crate::audio::AudioConfig;
use crate::config::{
    UiPrefs, APP_VERSION, DEVICE_HOSTNAME, SETUP_AP_IP, SETUP_AP_SSID, STA_HOSTNAME_PREFIX,
};
use crate::mqtt::MqttConfig;
use crate::ota::client::OtaStatus;
use crate::wifi::WlanConfig;

fn to_json<T: Serialize>(value: &T) -> String {
    for size in [256usize, 512, 1024, 2048, 4096, 8192] {
        let mut buf = vec![0u8; size];
        if let Ok(n) = serde_json_core::to_slice(value, &mut buf) {
            return match core::str::from_utf8(&buf[..n]) {
                Ok(s) => String::from(s),
                Err(_) => String::from("{}"),
            };
        }
    }
    String::from("{}")
}

#[derive(Serialize)]
struct ErrResp {
    ok: bool,
    error: String,
}

#[derive(Serialize)]
struct OkNextResp {
    ok: bool,
    next: String,
}

#[derive(Serialize)]
struct BatteryResp {
    #[serde(rename = "batteryMv")]
    battery_mv: i32,
    #[serde(rename = "batteryPct")]
    battery_pct: i32,
}

#[derive(Serialize)]
struct DeviceResp {
    hostname: String,
    version: String,
    mode: &'static str,
    #[serde(rename = "deviceId")]
    device_id: String,
    #[serde(rename = "batteryMv")]
    battery_mv: i32,
    #[serde(rename = "batteryPct")]
    battery_pct: i32,
    #[serde(rename = "apSsid", skip_serializing_if = "Option::is_none")]
    ap_ssid: Option<String>,
    #[serde(rename = "apIp", skip_serializing_if = "Option::is_none")]
    ap_ip: Option<String>,
}

#[derive(Serialize)]
struct WifiConnectedResp {
    connected: bool,
    ssid: String,
    ip: String,
    gateway: String,
    netmask: String,
    dns1: String,
    dns2: String,
    rssi: i32,
}

#[derive(Serialize)]
struct ChayaResp {
    rx: i32,
    tx: i32,
    connected: bool,
    configured: bool,
    paired: bool,
}

#[derive(Serialize)]
struct MqttStatusResp {
    connected: bool,
}

#[derive(Serialize)]
struct MqttConfigResp {
    #[serde(rename = "deviceId")]
    device_id: String,
    server: String,
    port: u16,
    tls: bool,
    username: String,
    #[serde(rename = "hasPassword")]
    has_password: bool,
    #[serde(rename = "topicPub")]
    topic_pub: String,
    #[serde(rename = "topicSub")]
    topic_sub: String,
    #[serde(rename = "partnerId")]
    partner_id: String,
    #[serde(rename = "pairingId")]
    pairing_id: String,
    #[serde(rename = "nvsOk")]
    nvs_ok: bool,
    #[serde(rename = "applyPending")]
    apply_pending: bool,
}

#[derive(Serialize)]
struct SettingsResp {
    #[serde(rename = "resetDays")]
    reset_days: u8,
    lang: String,
    theme: String,
    #[serde(rename = "ledEnabled")]
    led_enabled: bool,
    #[serde(rename = "audioTxEnabled")]
    audio_tx_enabled: bool,
    #[serde(rename = "audioRxEnabled")]
    audio_rx_enabled: bool,
    #[serde(rename = "audioTxVolume")]
    audio_tx_volume: u8,
    #[serde(rename = "audioRxVolume")]
    audio_rx_volume: u8,
    #[serde(rename = "quietHourStart")]
    quiet_hour_start: u8,
    #[serde(rename = "quietHourEnd")]
    quiet_hour_end: u8,
    #[serde(rename = "txHz")]
    tx_hz: u16,
    #[serde(rename = "txMs")]
    tx_ms: u16,
    #[serde(rename = "rxHz")]
    rx_hz: u16,
    #[serde(rename = "rxMs")]
    rx_ms: u16,
    #[serde(rename = "nvsOk")]
    nvs_ok: bool,
    #[serde(rename = "applyPending")]
    apply_pending: bool,
}

#[derive(Serialize)]
struct OtaStatusResp {
    phase: &'static str,
    channel: String,
    #[serde(rename = "localVersion")]
    local_version: String,
    #[serde(rename = "availableVersion")]
    available_version: String,
    #[serde(rename = "bytesDone")]
    bytes_done: u32,
    #[serde(rename = "bytesTotal")]
    bytes_total: u32,
    error: String,
    generation: u32,
}

#[derive(Serialize)]
struct ConnectStatusResp {
    state: String,
    ssid: String,
}

#[derive(Serialize)]
struct WifiConfigResp {
    ssid: String,
    mode: String,
    ip: String,
    gateway: String,
    netmask: String,
    dns1: String,
    dns2: String,
    ntp1: String,
    ntp2: String,
}

#[derive(Serialize)]
struct StatusResp {
    version: String,
    id: String,
    ap: bool,
    wifi: bool,
    mqtt: bool,
    #[serde(rename = "heartTx")]
    heart_tx: i32,
    #[serde(rename = "heartRx")]
    heart_rx: i32,
    #[serde(rename = "batteryPct")]
    battery_pct: i32,
    #[serde(rename = "batteryMv")]
    battery_mv: i32,
}

pub fn json_ok() -> &'static str {
    "{\"ok\":true}"
}

pub fn json_err(error: &str) -> String {
    to_json(&ErrResp {
        ok: false,
        error: String::from(error),
    })
}

pub fn json_ok_next(next: &str) -> String {
    to_json(&OkNextResp {
        ok: true,
        next: String::from(next),
    })
}

/// SSE `device` payload (C++ `fillDeviceBatteryJson`) — battery only.
pub fn device_battery_json(bat_mv: i32, bat_pct: i32) -> String {
    to_json(&BatteryResp {
        battery_mv: bat_mv,
        battery_pct: bat_pct.max(0).min(100),
    })
}

pub fn device_json(device_id: &str, ap_mode: bool, bat_mv: i32, bat_pct: i32) -> String {
    let hostname = if ap_mode {
        String::from(DEVICE_HOSTNAME)
    } else {
        let mut h = String::from(STA_HOSTNAME_PREFIX);
        h.push_str(device_id);
        h
    };
    to_json(&DeviceResp {
        hostname,
        version: String::from(APP_VERSION),
        mode: if ap_mode { "ap" } else { "sta" },
        device_id: String::from(device_id),
        battery_mv: bat_mv,
        battery_pct: bat_pct.max(0).min(100),
        ap_ssid: ap_mode.then(|| String::from(SETUP_AP_SSID)),
        ap_ip: ap_mode.then(|| String::from(SETUP_AP_IP)),
    })
}

/// OpenAPI `WifiConfig` (no password).
pub fn wifi_config_json(cfg: &WlanConfig) -> String {
    to_json(&WifiConfigResp {
        ssid: String::from(cfg.ssid.as_str()),
        mode: String::from(cfg.mode.as_str()),
        ip: String::from(cfg.ip.as_str()),
        gateway: String::from(cfg.gateway.as_str()),
        netmask: String::from(cfg.netmask.as_str()),
        dns1: String::from(cfg.dns1.as_str()),
        dns2: String::from(cfg.dns2.as_str()),
        ntp1: String::from(cfg.ntp1.as_str()),
        ntp2: String::from(cfg.ntp2.as_str()),
    })
}

/// Legacy `/api/status` snapshot (also used for compact SSE).
pub fn status_json(
    device_id: &str,
    ap_mode: bool,
    wifi_ok: bool,
    mqtt_ok: bool,
    heart_tx: i32,
    heart_rx: i32,
    bat_pct: i32,
    bat_mv: i32,
) -> String {
    to_json(&StatusResp {
        version: String::from(APP_VERSION),
        id: String::from(device_id),
        ap: ap_mode,
        wifi: wifi_ok,
        mqtt: mqtt_ok,
        heart_tx,
        heart_rx,
        battery_pct: bat_pct,
        battery_mv: bat_mv,
    })
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
    to_json(&WifiConnectedResp {
        connected: true,
        ssid: String::from(ssid),
        ip: String::from(ip),
        gateway: String::from(gateway),
        netmask: String::from(netmask),
        dns1: String::from(dns1),
        dns2: String::from(dns2),
        rssi,
    })
}

pub fn chaya_json(rx: i32, tx: i32, connected: bool, configured: bool, paired: bool) -> String {
    to_json(&ChayaResp {
        rx,
        tx,
        connected,
        configured,
        paired,
    })
}

pub fn mqtt_status_json(connected: bool) -> String {
    to_json(&MqttStatusResp { connected })
}

pub fn mqtt_config_json(device_id: &str, cfg: &MqttConfig) -> String {
    let pub_topic = cfg.pub_topic(device_id);
    let sub_topic = cfg.sub_topic();
    to_json(&MqttConfigResp {
        device_id: String::from(device_id),
        server: String::from(cfg.host.as_str()),
        port: cfg.port,
        tls: cfg.port >= 8883,
        username: String::from(cfg.user.as_str()),
        has_password: !cfg.pass.is_empty(),
        topic_pub: String::from(pub_topic.as_str()),
        topic_sub: String::from(sub_topic.as_str()),
        partner_id: String::from(cfg.partner_id.as_str()),
        pairing_id: String::from(cfg.pairing_id.as_str()),
        nvs_ok: true,
        apply_pending: false,
    })
}

pub fn settings_json(audio: &AudioConfig, ui: &UiPrefs) -> String {
    to_json(&SettingsResp {
        reset_days: ui.reset_days,
        lang: String::from(ui.lang.as_str()),
        theme: String::from(ui.theme.as_str()),
        led_enabled: ui.led_enabled,
        audio_tx_enabled: audio.tx_enabled,
        audio_rx_enabled: audio.rx_enabled,
        audio_tx_volume: audio.volume,
        audio_rx_volume: audio.volume,
        quiet_hour_start: audio.quiet_start,
        quiet_hour_end: audio.quiet_end,
        tx_hz: 880,
        tx_ms: 80,
        rx_hz: 660,
        rx_ms: 80,
        nvs_ok: true,
        apply_pending: false,
    })
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
    to_json(&OtaStatusResp {
        phase,
        channel: String::from(channel),
        local_version: String::from(APP_VERSION),
        available_version: String::from(st.last_tag.as_str()),
        bytes_done: 0,
        bytes_total: 0,
        error: String::from(st.last_error.as_str()),
        generation: 0,
    })
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
    to_json(&ConnectStatusResp {
        state: String::from(state),
        ssid: String::from(ssid),
    })
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

pub fn parse_wlan_connect(
    body: &[u8],
    cfg: &mut WlanConfig,
    password_present: &mut bool,
) -> Result<(), &'static str> {
    use super::dto::{from_slice, WifiConnectReq};

    let req = from_slice::<WifiConnectReq>(body).map_err(|_| "json")?;
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
    Ok(())
}

fn push_opt<const N: usize>(dst: &mut heapless::String<N>, v: Option<&str>) {
    if let Some(s) = v {
        dst.clear();
        let _ = dst.push_str(s);
    }
}
