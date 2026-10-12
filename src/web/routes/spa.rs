//! spa routes.

use alloc::string::String;
use edge_http::Method;

use crate::web::shared::HttpShared;
use crate::web::spa;

pub async fn handle(
    _s: &HttpShared,
    method: Method,
    path: &str,
    _body: &[u8],
    _mqtt_ok: bool,
    _heart_tx: i32,
    _heart_rx: i32,
    _bat_pct: i32,
    _bat_mv: i32,
) -> Option<(u16, String, &'static str)> {
    if path == "/openapi.json" && method == Method::Get {
        return Some((200, String::from(spa::OPENAPI_JSON), "application/json"));
    }

    if (path == "/" || path == "/index.html") && method == Method::Get {
        return Some((200, String::from(spa::ADMIN_HTML), "text/html"));
    }

    None
}
