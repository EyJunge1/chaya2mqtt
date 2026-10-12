//! system routes.

use alloc::string::String;
use edge_http::Method;

use crate::config::{NS_CFG, NS_CHAYA, NS_MQTT, NS_WIFI};
use crate::core::audio::AudioKind;
use crate::led::LedPreset;
use crate::util::platform::KvStore;
use crate::web::json;
use crate::web::shared::HttpShared;
use crate::web::sse;

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
    if (path == "/api/chaya/send" || path == "/api/send") && method == Method::Post && !s.ap_mode {
        // Request only — HEART_TX / HEART_DIRTY apply after MQTT PUBACK (C++ parity).
        // LED TxPending now; TxAck/TxFail from MQTT session on PUBACK/timeout.
        s.heart_bump.signal(1);
        s.audio_beep.signal(AudioKind::Tx);
        s.led_preset.signal(LedPreset::TxPending);
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
