//! ota routes.

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

    if (path == "/api/update/check" || path == "/api/ota/check") && method == Method::Post && !s.ap_mode {
        if let Ok(req) = crate::http::json::from_slice::<crate::http::json::UpdateCheckReq>(body) {
            if let Some(ch) = req.channel {
                let channel = OtaChannel::parse(ch);
                let mut kv = s.kv.lock().await;
                let mut ota = OtaService::load(&mut *kv);
                ota.channel = channel;
                let _ = ota.save_channel(&mut *kv);
                let mut st = s.ota_status.lock().await;
                st.channel.clear();
                let _ = st.channel.push_str(channel.as_str());
            }
        }
        s.ota_check.signal(());
        return Some((200, String::from(json::json_ok()), "application/json"));
    }

    if (path == "/api/update/install" || path == "/api/ota/install")
        && method == Method::Post
        && !s.ap_mode
    {
        s.ota_install.signal(());
        return Some((200, String::from(json::json_ok()), "application/json"));
    }

    if (path == "/api/update/status" || path == "/api/ota/status") && method == Method::Get {
        let st = s.ota_status.lock().await;
        return Some((200, json::ota_status_json(&st), "application/json"));
    }

    None
}
