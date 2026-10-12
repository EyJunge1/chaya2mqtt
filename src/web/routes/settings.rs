//! settings routes.

use alloc::string::String;
use edge_http::Method;

use crate::config::UiPrefs;
use crate::ota::{OtaChannel, OtaService};
use crate::util::platform::KvStore;
use crate::web::form;
use crate::web::json;
use crate::web::shared::HttpShared;

pub async fn handle(
    s: &HttpShared,
    method: Method,
    path: &str,
    body: &[u8],
    _mqtt_ok: bool,
    _heart_tx: i32,
    _heart_rx: i32,
    _bat_pct: i32,
    _bat_mv: i32,
) -> Option<(u16, String, &'static str)> {
    if path == "/api/settings" && method == Method::Get {
        let audio = s.audio_cfg.lock().await.clone();
        let ui = {
            let mut kv = s.kv.lock().await;
            UiPrefs::load(&mut *kv)
        };
        return Some((200, json::settings_json(&audio, &ui), "application/json"));
    }

    if path == "/api/settings" && method == Method::Post {
        let mut audio = s.audio_cfg.lock().await.clone();
        let mut ui = {
            let mut kv = s.kv.lock().await;
            UiPrefs::load(&mut *kv)
        };
        if let Ok(req) = crate::web::json::from_slice::<crate::web::json::SettingsSaveReq>(body) {
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
            if let Some(v) = req.reset_days.or(req.resetDays) {
                if v <= 30 {
                    ui.reset_days = v;
                }
            }
            if let Some(v) = req.led_enabled.or(req.ledEnabled) {
                ui.led_enabled = v;
            }
            if let Some(lang) = req.lang {
                if lang == "de" || lang == "en" {
                    ui.lang.clear();
                    let _ = ui.lang.push_str(lang);
                }
            }
            if let Some(theme) = req.theme {
                if theme == "system" || theme == "dark" || theme == "light" {
                    ui.theme.clear();
                    let _ = ui.theme.push_str(theme);
                }
            }
        } else if let Some(ch) = form::form_value(body, "otaChannel") {
            let channel = OtaChannel::parse(ch.as_str());
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
            let _ = ui.save(&mut *kv);
        }
        *s.audio_cfg.lock().await = audio;
        *s.led_enabled.lock().await = ui.led_enabled;
        return Some((200, String::from(json::json_ok()), "application/json"));
    }

    None
}
