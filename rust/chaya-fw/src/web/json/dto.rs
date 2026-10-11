//! Request DTOs parsed with `serde-json-core` (SPA OpenAPI field names).

use serde::Deserialize;

#[derive(Debug, Deserialize, Default)]
pub struct WifiConnectReq<'a> {
    pub ssid: Option<&'a str>,
    pub password: Option<&'a str>,
    pub mode: Option<&'a str>,
    pub ip: Option<&'a str>,
    pub gateway: Option<&'a str>,
    pub netmask: Option<&'a str>,
    pub dns1: Option<&'a str>,
    pub dns2: Option<&'a str>,
    pub ntp1: Option<&'a str>,
    pub ntp2: Option<&'a str>,
}

#[derive(Debug, Deserialize, Default)]
pub struct MqttSaveReq<'a> {
    pub mqtt_server: Option<&'a str>,
    pub server: Option<&'a str>,
    pub mqtt_port: Option<u16>,
    pub port: Option<u16>,
    pub mqtt_tls: Option<bool>,
    pub tls: Option<bool>,
    pub mqtt_user: Option<&'a str>,
    pub username: Option<&'a str>,
    pub mqtt_pass: Option<&'a str>,
    pub password: Option<&'a str>,
    #[serde(alias = "partnerId")]
    pub partner_id: Option<&'a str>,
    #[serde(alias = "pairingId")]
    pub pairing_id: Option<&'a str>,
}

#[derive(Debug, Deserialize, Default)]
#[allow(non_snake_case)]
pub struct SettingsSaveReq<'a> {
    pub quiet_hour_start: Option<u8>,
    pub quietHourStart: Option<u8>,
    pub quietStart: Option<u8>,
    pub quiet_hour_end: Option<u8>,
    pub quietHourEnd: Option<u8>,
    pub quietEnd: Option<u8>,
    pub audio_tx_volume: Option<u8>,
    pub audioTxVolume: Option<u8>,
    pub volume: Option<u8>,
    pub audio_tx_enabled: Option<bool>,
    pub audio_rx_enabled: Option<bool>,
    pub audioTxEnabled: Option<bool>,
    pub audioRxEnabled: Option<bool>,
    pub otaChannel: Option<&'a str>,
    pub channel: Option<&'a str>,
}

#[derive(Debug, Deserialize, Default)]
pub struct UpdateCheckReq<'a> {
    pub channel: Option<&'a str>,
}

pub fn from_slice<'a, T: Deserialize<'a>>(body: &'a [u8]) -> Result<T, ()> {
    serde_json_core::from_slice(body).map(|(v, _)| v).map_err(|_| ())
}
