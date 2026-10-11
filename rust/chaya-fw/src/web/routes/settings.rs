//! settings routes.

use alloc::string::String;
use edge_http::Method;
use crate::http::form;
use crate::http::json;
use crate::http::shared::HttpShared;
use crate::http::sse;
use crate::http::spa;
use crate::mqtt::MqttConfig;
use crate::ota::{OtaChannel, OtaService};
use crate::storage::platform::KvStore;
use crate::wifi::WlanConfig;
use chaya_pure::audio::AudioKind;

pub async fn handle(
    s: &HttpShared,
    method: Method,
    path: &str,
    body: &[u8],
    mqtt_ok: bool,
    heart_tx: i32,
    heart_rx: i32,
    bat_pct: i32,
    bat_mv: i32,
) -> Option<(u16, String, &'static str)> {
    use alloc::string::String;

    if path == "/api/settings" && method == Method::Get {
        let audio = s.audio_cfg.lock().await.clone();
        return Some((200, json::settings_json(&audio, true), "application/json"));
    }

    if path == "/api/settings" && method == Method::Post {
        let mut audio = s.audio_cfg.lock().await.clone();
        if let Ok(req) = crate::http::json::from_slice::<crate::http::json::SettingsSaveReq>(body) {
            if let Some(ch) = req.otaChannel.or(req.channel) {
                let channel = OtaChannel::parse(ch);
                let mut kv = s.kv.lock().await;
                let mut ota = OtaService::load(&mut *kv);
                ota.channel = channel;
                let _ = ota.save_channel(&mut *kv);
                let mut st = s.ota_status.lock().await;
                st.channel.clear();
                let _ = st.channel.push_str(channel.as_str());
            }
            if let Some(v) = req
                .quiet_hour_start
                .or(req.quietHourStart)
                .or(req.quietStart)
            {
                if v <= 23 {
                    audio.quiet_start = v;
                }
            }
            if let Some(v) = req.quiet_hour_end.or(req.quietHourEnd).or(req.quietEnd) {
                if v <= 23 {
                    audio.quiet_end = v;
                }
            }
            if let Some(v) = req.audio_tx_volume.or(req.audioTxVolume).or(req.volume) {
                if v <= 100 {
                    audio.volume = v;
                }
            }
            if let Some(v) = req.audio_tx_enabled.or(req.audioTxEnabled) {
                audio.tx_enabled = v;
            }
            if let Some(v) = req.audio_rx_enabled.or(req.audioRxEnabled) {
                audio.rx_enabled = v;
            }
        } else if let Some(ch) = form::form_value(body, "otaChannel") {
            let channel = OtaChannel::parse(ch);
            let mut kv = s.kv.lock().await;
            let mut ota = OtaService::load(&mut *kv);
            ota.channel = channel;
            let _ = ota.save_channel(&mut *kv);
            let mut st = s.ota_status.lock().await;
            st.channel.clear();
            let _ = st.channel.push_str(channel.as_str());
        }
        {
            let mut kv = s.kv.lock().await;
            let _ = audio.save(&mut *kv);
        }
        *s.audio_cfg.lock().await = audio;
        return Some((200, String::from(json::json_ok()), "application/json"));
    }

    None
}
