//! bootstrap routes.

use alloc::string::String;
use edge_http::Method;

use crate::mqtt::MqttConfig;
use crate::util::platform::KvStore;
use crate::web::json;
use crate::web::shared::HttpShared;

pub async fn handle(
    s: &HttpShared,
    method: Method,
    path: &str,
    _body: &[u8],
    mqtt_ok: bool,
    heart_tx: i32,
    heart_rx: i32,
    bat_pct: i32,
    bat_mv: i32,
) -> Option<(u16, String, &'static str)> {
    use super::wifi_status_json;

    if path == "/api/bootstrap" && method == Method::Get {
        let wifi = wifi_status_json(s).await;
        let (mqtt_cfg, ui) = {
            let mut kv = s.kv.lock().await;
            (
                MqttConfig::load(&mut *kv),
                crate::config::UiPrefs::load(&mut *kv),
            )
        };
        let audio = s.audio_cfg.lock().await.clone();
        let ota = s.ota_status.lock().await.clone();
        let body = json::bootstrap_json(
            s.device_id,
            s.ap_mode,
            bat_mv,
            bat_pct,
            &wifi,
            if s.ap_mode { None } else { Some(&mqtt_cfg) },
            mqtt_ok,
            heart_tx,
            heart_rx,
            &audio,
            &ui,
            &ota,
        );
        return Some((200, body, "application/json"));
    }

    None
}
