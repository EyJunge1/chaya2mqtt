//! HTTP route dispatch (SPA OpenAPI paths).

mod bootstrap;
mod mqtt;
mod ota;
mod settings;
mod spa;
mod system;
mod wifi;
mod wifi_status;

pub use wifi_status::wifi_status_json;

use alloc::string::String;
use edge_http::Method;

use crate::web::shared::HttpShared;

pub async fn dispatch(
    s: &HttpShared,
    method: Method,
    path: &str,
    body: &[u8],
    mqtt_ok: bool,
    heart_tx: i32,
    heart_rx: i32,
    bat_pct: i32,
    bat_mv: i32,
) -> (u16, String, &'static str) {
    if let Some(r) = bootstrap::handle(
        s, method, path, body, mqtt_ok, heart_tx, heart_rx, bat_pct, bat_mv,
    )
    .await
    {
        return r;
    }
    if let Some(r) = wifi::handle(
        s, method, path, body, mqtt_ok, heart_tx, heart_rx, bat_pct, bat_mv,
    )
    .await
    {
        return r;
    }
    if let Some(r) = mqtt::handle(
        s, method, path, body, mqtt_ok, heart_tx, heart_rx, bat_pct, bat_mv,
    )
    .await
    {
        return r;
    }
    if let Some(r) = settings::handle(
        s, method, path, body, mqtt_ok, heart_tx, heart_rx, bat_pct, bat_mv,
    )
    .await
    {
        return r;
    }
    if let Some(r) = system::handle(
        s, method, path, body, mqtt_ok, heart_tx, heart_rx, bat_pct, bat_mv,
    )
    .await
    {
        return r;
    }
    if let Some(r) = ota::handle(
        s, method, path, body, mqtt_ok, heart_tx, heart_rx, bat_pct, bat_mv,
    )
    .await
    {
        return r;
    }
    if let Some(r) = spa::handle(
        s, method, path, body, mqtt_ok, heart_tx, heart_rx, bat_pct, bat_mv,
    )
    .await
    {
        return r;
    }
    (
        404,
        String::from("{\"ok\":false,\"error\":\"not_found\"}"),
        "application/json",
    )
}
