//! system routes.

use alloc::string::String;
use edge_http::Method;
use crate::web::form;
use crate::config::{NS_CFG, NS_CHAYA, NS_MQTT, NS_WIFI};
use crate::web::json;
use crate::web::shared::HttpShared;
use crate::web::sse;
use crate::web::spa;
use crate::mqtt::MqttConfig;
use crate::ota::{OtaChannel, OtaService};
use crate::util::platform::KvStore;
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

    if (path == "/api/chaya/send" || path == "/api/send") && method == Method::Post && !s.ap_mode {
        let next = {
            let mut t = s.heart_tx.lock().await;
            *t = t.saturating_add(1);
            *t
        };
        s.heart_bump.signal(next);
        s.audio_beep.signal(AudioKind::Tx);
        s.heart_dirty.signal(());
        s.epd_redraw.signal(());
        return Some((202, String::from(json::json_ok()), "application/json"));
    }

    if path == "/api/reboot" && method == Method::Post {
        s.reboot.signal(());
        return Some((200, String::from(json::json_ok()), "application/json"));
    }

    if path == "/api/factory-reset" && method == Method::Post {
        let mut kv = s.kv.lock().await;
        let _ = kv.erase_ns(NS_WIFI);
        let _ = kv.erase_ns(NS_MQTT);
        let _ = kv.erase_ns(NS_CHAYA);
        let _ = kv.erase_ns(NS_CFG);
        s.reboot.signal(());
        return Some((200, String::from(json::json_ok()), "application/json"));
    }

    if path == "/api/status" && method == Method::Get {
        let body = sse::json_status(
            s.device_id,
            s.ap_mode,
            !s.ap_mode,
            mqtt_ok,
            heart_tx,
            heart_rx,
            bat_pct,
            bat_mv,
        );
        return Some((200, String::from(body.as_str()), "application/json"));
    }

    None
}
