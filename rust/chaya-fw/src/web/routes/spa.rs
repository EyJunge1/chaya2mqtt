//! spa routes.

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

    if path == "/openapi.json" && method == Method::Get {
        return Some((200, String::from(spa::OPENAPI_JSON), "application/json"));
    }

    if (path == "/" || path == "/index.html") && method == Method::Get {
        return Some((200, String::from(spa::ADMIN_HTML), "text/html"));
    }

    None
}
