//! wifi routes.

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

    if path == "/api/wifi/config" && method == Method::Get {
        let mut kv = s.kv.lock().await;
        let cfg = WlanConfig::load(&mut *kv);
        return Some((200, String::from(cfg.to_config_json().as_str()), "application/json"));
    }

    if path == "/api/wifi/scan" && method == Method::Get {
        let cache = s.wifi_scan.lock().await;
        return Some((200, cache.to_json(), "application/json"));
    }

    if path == "/api/wifi/scan" && method == Method::Post {
        let owns = *s.wifi_test_owns.lock().await;
        let ota = s.ota_status.lock().await;
        let busy = ota.checking || ota.installing;
        drop(ota);
        let mut cache = s.wifi_scan.lock().await;
        match cache.request_kick(owns, busy) {
            Ok(()) => {
                s.scan_kick.signal(());
                return Some((202, String::from(json::json_ok()), "application/json"));
            }
            Err(e) => {
                return Some((503, json::json_err(e), "application/json"));
            }
        }
    }

    if path == "/api/wifi/connect" && method == Method::Post {
        let mut cfg = WlanConfig::default();
        let mut password_present = false;
        if let Err(e) = json::parse_wlan_connect(body, &mut cfg, &mut password_present) {
            return Some((400, json::json_err(e), "application/json"));
        }
        if !password_present && cfg.pass.is_empty() {
            let mut kv = s.kv.lock().await;
            let stored = WlanConfig::load(&mut *kv);
            if stored.ssid == cfg.ssid {
                cfg.pass = stored.pass;
            }
        }
        if s.ap_mode {
            {
                let mut pending = s.wifi_pending.lock().await;
                *pending = cfg.clone();
                let mut st = s.wifi_test_state.lock().await;
                st.clear();
                let _ = st.push_str("testing");
                let mut ss = s.wifi_test_ssid.lock().await;
                ss.clear();
                let _ = ss.push_str(cfg.ssid.as_str());
                *s.wifi_test_owns.lock().await = true;
            }
            s.wifi_test_kick.signal(());
            return Some((
                200,
                json::json_ok_next("/wifi-testing"),
                "application/json",
            ));
        }
        {
            let mut kv = s.kv.lock().await;
            if !cfg.save(&mut *kv) {
                return Some((500, json::json_err("save"), "application/json"));
            }
        }
        s.reboot.signal(());
        return Some((200, String::from(json::json_ok()), "application/json"));
    }

    if path == "/api/wifi/connect-status" && method == Method::Get {
        let st = s.wifi_test_state.lock().await.clone();
        let ssid = s.wifi_test_ssid.lock().await.clone();
        let state = if st.is_empty() { "idle" } else { st.as_str() };
        return Some((
            200,
            json::connect_status_json(state, ssid.as_str()),
            "application/json",
        ));
    }

    if path == "/api/wifi/connect-commit" && method == Method::Post && s.ap_mode {
        let st = s.wifi_test_state.lock().await.clone();
        if st.as_str() != "ok" {
            return Some((400, json::json_err("state"), "application/json"));
        }
        let pending = s.wifi_pending.lock().await.clone();
        {
            let mut kv = s.kv.lock().await;
            if !pending.save(&mut *kv) {
                return Some((500, json::json_err("save"), "application/json"));
            }
        }
        *s.wifi_test_owns.lock().await = false;
        s.reboot.signal(());
        return Some((200, String::from(json::json_ok()), "application/json"));
    }

    if path == "/api/wifi/connect-abort" && method == Method::Post && s.ap_mode {
        *s.wifi_test_owns.lock().await = false;
        let mut st = s.wifi_test_state.lock().await;
        st.clear();
        let _ = st.push_str("idle");
        return Some((200, String::from(json::json_ok()), "application/json"));
    }

    if path == "/api/wifi/connect-retry" && method == Method::Post && s.ap_mode {
        let mut st = s.wifi_test_state.lock().await;
        st.clear();
        let _ = st.push_str("testing");
        *s.wifi_test_owns.lock().await = true;
        s.wifi_test_kick.signal(());
        return Some((200, String::from(json::json_ok()), "application/json"));
    }

    // Legacy form POST /api/wifi
    if path == "/api/wifi" && method == Method::Post && s.ap_mode {
        let mut cfg = WlanConfig::default();
        if let Some(ssid) = form::form_value(body, "ssid") {
            let _ = cfg.ssid.push_str(ssid);
        }
        if let Some(pass) = form::form_value(body, "pass") {
            let _ = cfg.pass.push_str(pass);
        }
        let mut kv = s.kv.lock().await;
        if cfg.has_sta() {
            let _ = cfg.save(&mut *kv);
        }
        s.reboot.signal(());
        return Some((302, String::new(), ""));
    }

    None
}
