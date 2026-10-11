//! mqtt routes.

use alloc::string::String;
use edge_http::Method;
use crate::web::form;
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

    if path == "/api/mqtt" && method == Method::Get {
        let mut kv = s.kv.lock().await;
        let cfg = MqttConfig::load(&mut *kv);
        return Some((
            200,
            json::mqtt_config_json(s.device_id, &cfg),
            "application/json",
        ));
    }

    if path == "/api/mqtt" && method == Method::Post {
        let mut kv = s.kv.lock().await;
        let mut cfg = MqttConfig::load(&mut *kv);
        if let Ok(req) = crate::web::json::from_slice::<crate::web::json::MqttSaveReq>(body) {
            if let Some(h) = req.mqtt_server.or(req.server) {
                cfg.host.clear();
                let _ = cfg.host.push_str(h);
            }
            if let Some(p) = req.mqtt_port.or(req.port) {
                if p > 0 {
                    cfg.port = p;
                }
            }
            if let Some(tls) = req.mqtt_tls.or(req.tls) {
                if tls && cfg.port < 8883 {
                    cfg.port = 8883;
                }
                if !tls && cfg.port == 8883 {
                    cfg.port = 1883;
                }
            }
            if let Some(u) = req.mqtt_user.or(req.username) {
                cfg.user.clear();
                let _ = cfg.user.push_str(u);
            }
            if let Some(p) = req.mqtt_pass.or(req.password) {
                cfg.pass.clear();
                let _ = cfg.pass.push_str(p);
            }
            if let Some(p) = req.partner_id {
                cfg.partner_id.clear();
                let _ = cfg.partner_id.push_str(p);
            }
            if let Some(p) = req.pairing_id {
                cfg.pairing_id.clear();
                let _ = cfg.pairing_id.push_str(p);
            }
        } else {
            // Legacy form body.
            if let Some(h) = form::form_value(body, "host") {
                cfg.host.clear();
                let _ = cfg.host.push_str(h);
            }
            if let Some(p) = form::form_value(body, "port").and_then(|s| s.parse().ok()) {
                if p > 0u16 {
                    cfg.port = p;
                }
            }
            if let Some(u) = form::form_value(body, "user") {
                cfg.user.clear();
                let _ = cfg.user.push_str(u);
            }
            if let Some(p) = form::form_value(body, "pass") {
                cfg.pass.clear();
                let _ = cfg.pass.push_str(p);
            }
            if let Some(p) = form::form_value(body, "partner") {
                cfg.partner_id.clear();
                let _ = cfg.partner_id.push_str(p);
            }
        }
        let _ = cfg.save(&mut *kv);
        return Some((200, String::from(json::json_ok()), "application/json"));
    }

    None
}
