//! ota routes.

use alloc::string::String;
use edge_http::Method;

use crate::ota::{OtaChannel, OtaService};
use crate::util::platform::KvStore;
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
    if (path == "/api/update/check" || path == "/api/ota/check")
        && method == Method::Post
        && !s.ap_mode
    {
        let Ok(req) = crate::web::json::from_slice::<crate::web::json::UpdateCheckReq>(body) else {
            return Some((400, json::json_err("channel"), "application/json"));
        };
        let Some(ch) = req.channel else {
            return Some((400, json::json_err("channel"), "application/json"));
        };
        let Some(channel) = OtaChannel::try_parse(ch) else {
            return Some((400, json::json_err("channel"), "application/json"));
        };
        {
            let mut st = s.ota_status.lock().await;
            if st.checking || st.installing {
                return Some((503, json::json_err("busy"), "application/json"));
            }
            st.channel.clear();
            let _ = st.channel.push_str(channel.as_str());
        }
        let mut kv = s.kv.lock().await;
        let mut ota = OtaService::load(&mut *kv);
        ota.channel = channel;
        let _ = ota.save_channel(&mut *kv);
        s.ota_check.signal(());
        return Some((200, String::from(json::json_ok()), "application/json"));
    }

    if (path == "/api/update/install" || path == "/api/ota/install")
        && method == Method::Post
        && !s.ap_mode
    {
        let st = s.ota_status.lock().await;
        if st.checking || st.installing {
            return Some((409, json::json_err("busy"), "application/json"));
        }
        if !st.flash_ready {
            return Some((409, json::json_err("not_available"), "application/json"));
        }
        drop(st);
        s.ota_install.signal(());
        return Some((200, String::from(json::json_ok()), "application/json"));
    }

    if (path == "/api/update/status" || path == "/api/ota/status") && method == Method::Get {
        let st = s.ota_status.lock().await;
        return Some((200, json::ota_status_json(&st), "application/json"));
    }

    None
}
