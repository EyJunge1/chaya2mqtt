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
use chaya_pure::mqtt::{
    mqtt_derived_topics_valid, mqtt_server_syntax_ok, normalize_hex_id, pairing_id_input_valid,
    partner_id_input_valid, MQTT_SERVER_BUF_LEN, MQTT_TOPIC_BUF_LEN,
};

fn normalize_hex_id_field(s: &mut heapless::String<8>) {
    let mut tmp = [0u8; 8];
    let n = s.len().min(tmp.len());
    tmp[..n].copy_from_slice(s.as_bytes());
    normalize_hex_id(&mut tmp[..n]);
    s.clear();
    if let Ok(t) = core::str::from_utf8(&tmp[..n]) {
        let _ = s.push_str(t);
    }
}

fn mqtt_post_err(code: &'static str) -> (u16, String, &'static str) {
    (400, json::json_err(code), "application/json")
}

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
        let mut pairing_touched = false;
        let mut partner_touched = false;

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
                partner_touched = true;
            }
            if let Some(p) = req.pairing_id {
                cfg.pairing_id.clear();
                let _ = cfg.pairing_id.push_str(p);
                pairing_touched = true;
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
                partner_touched = true;
            }
        }

        let own_id = s.device_id;

        if pairing_touched {
            normalize_hex_id_field(&mut cfg.pairing_id);
            if !pairing_id_input_valid(cfg.pairing_id.as_str()) {
                return Some(mqtt_post_err("pairing"));
            }
            if !cfg.pairing_id.is_empty() && cfg.pairing_id.as_str() == own_id {
                cfg.pairing_id.clear();
            }
        }
        if partner_touched {
            normalize_hex_id_field(&mut cfg.partner_id);
            if !cfg.partner_id.is_empty()
                && !partner_id_input_valid(
                    cfg.partner_id.as_str(),
                    cfg.pairing_id.as_str(),
                    own_id,
                )
            {
                return Some(mqtt_post_err("partner"));
            }
        }
        if !cfg.partner_id.is_empty()
            && !cfg.pairing_id.is_empty()
            && cfg.partner_id.as_str() == cfg.pairing_id.as_str()
        {
            return Some(mqtt_post_err("partner"));
        }

        // C++ `mqttServerSyntaxOk` — Rust host capacity is 64; still enforce printable host rules.
        let server_max = core::cmp::min(MQTT_SERVER_BUF_LEN, cfg.host.capacity() + 1);
        if !mqtt_server_syntax_ok(cfg.host.as_str(), server_max) {
            return Some(mqtt_post_err("broker"));
        }

        let topic_pub = cfg.pub_topic(own_id);
        let topic_sub = cfg.sub_topic();
        if let Err(e) = mqtt_derived_topics_valid(
            topic_pub.as_str(),
            topic_sub.as_str(),
            !cfg.partner_id.is_empty(),
            MQTT_TOPIC_BUF_LEN,
        ) {
            return Some(mqtt_post_err(e));
        }

        let _ = cfg.save(&mut *kv);
        crate::sse_dirty::sse_mark(
            chaya_pure::constants::SSE_CHAYA | chaya_pure::constants::SSE_MQTT,
        );
        return Some((200, String::from(json::json_ok()), "application/json"));
    }

    None
}
