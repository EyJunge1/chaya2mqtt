//! edge-http admin server (AsyncWebServer replacement).

use core::fmt::{Debug, Display, Write as _};
use core::net::{IpAddr, Ipv4Addr, SocketAddr};

use chaya_pure::web::web_host_allowed;
use edge_http::io::server::{Connection, DefaultServer, Handler};
use edge_http::io::Error as HttpError;
use edge_http::Method;
use edge_nal::{TcpBind, TcpSplit};
use edge_nal_embassy::{Tcp, TcpBuffers};
use embedded_io_async::{Read, Write};
use esp_println::println;

use crate::web::json;
use crate::web::routes;
use crate::web::shared::HttpShared;
use crate::web::spa;
use crate::web::sse;
use crate::config::SETUP_AP_SSID;

pub struct AdminHandler {
    pub shared: &'static HttpShared,
}

impl Handler for AdminHandler {
    type Error<E>
        = HttpError<E>
    where
        E: Debug;

    async fn handle<T, const N: usize>(
        &self,
        _task_id: impl Display + Copy,
        conn: &mut Connection<'_, T, N>,
    ) -> Result<(), Self::Error<T::Error>>
    where
        T: Read + Write + TcpSplit,
    {
        let (method, path, host) = {
            let headers = conn.headers()?;
            let host = headers.headers.host().unwrap_or("");
            (headers.method, headers.path, host)
        };
        let mut path_buf = heapless::String::<128>::new();
        let _ = path_buf.push_str(path.split('?').next().unwrap_or(path));
        let path = path_buf.as_str();
        let mut host_buf = heapless::String::<64>::new();
        let _ = host_buf.push_str(host);
        let host = host_buf.as_str();

        let s = self.shared;

        // Captive OS probes use foreign Host headers — skip SEC-04 allowlist (C++ skipServerMiddlewares).
        if s.ap_mode && sse::is_captive_probe(path) && method == Method::Get {
            conn.initiate_response(
                302,
                Some("Found"),
                &[("Location", sse::captive_landing())],
            )
            .await?;
            return Ok(());
        }
        if s.ap_mode && path == "/success.txt" && method == Method::Get {
            conn.initiate_response(200, Some("OK"), &[]).await?;
            return Ok(());
        }
        if s.ap_mode && path == "/wpad.dat" && method == Method::Get {
            conn.initiate_response(404, Some("Not Found"), &[]).await?;
            return Ok(());
        }

        // SEC-04: Host allowlist
        let mut sta_ip_buf = heapless::String::<16>::new();
        let sta_ip = if !s.ap_mode {
            if let Some(cfg) = s.stack.config_v4() {
                let _ = write!(&mut sta_ip_buf, "{}", cfg.address.address());
                Some(sta_ip_buf.as_str())
            } else {
                None
            }
        } else {
            None
        };
        if !web_host_allowed(host, s.ap_mode, s.device_hostname, sta_ip) {
            conn.initiate_response(
                403,
                Some("Forbidden"),
                &[("Content-Type", "application/json")],
            )
            .await?;
            conn.write_all(b"{\"ok\":false,\"error\":\"host\"}").await?;
            return Ok(());
        }

        let mut body_buf = [0u8; 1024];
        let body_len = {
            let (_hdrs, body) = conn.split();
            let mut n = 0usize;
            loop {
                if n >= body_buf.len() {
                    break;
                }
                match body.read(&mut body_buf[n..]).await {
                    Ok(0) => break,
                    Ok(k) => n += k,
                    Err(_) => break,
                }
            }
            n
        };
        let body = &body_buf[..body_len];

        let mqtt_ok = *s.mqtt_ok.lock().await;
        let heart_tx = *s.heart_tx.lock().await;
        let heart_rx = *s.heart_rx.lock().await;
        let bat_pct = *s.bat_pct.lock().await;
        let bat_mv = *s.bat_mv.lock().await;

        if (path == "/events" || path == "/api/events") && method == Method::Get {
            conn.initiate_response(
                200,
                Some("OK"),
                &[
                    ("Content-Type", "text/event-stream"),
                    ("Cache-Control", "no-cache"),
                    ("Connection", "keep-alive"),
                ],
            )
            .await?;
            if run_sse_hub(conn, s).await.is_err() {
                // Client dropped or write failed — end stream.
            }
            return Ok(());
        }

        // Gzip SPA blob assets (paths with leading slash; index.html is not in the blob).
        if method == Method::Get && path != "/openapi.json" {
            let lookup = if path == "/" { "/index.html" } else { path };
            if let Some(asset) = spa::find_asset(lookup).or_else(|| {
                if lookup != path {
                    spa::find_asset(path)
                } else {
                    None
                }
            }) {
                let bytes = spa::asset_bytes(asset);
                let cache = if asset.path.starts_with("/assets/") {
                    "public, max-age=31536000, immutable"
                } else {
                    "no-cache"
                };
                if asset.gzip {
                    conn.initiate_response(
                        200,
                        Some("OK"),
                        &[
                            ("Content-Type", asset.content_type),
                            ("Content-Encoding", "gzip"),
                            ("Cache-Control", cache),
                        ],
                    )
                    .await?;
                } else {
                    conn.initiate_response(
                        200,
                        Some("OK"),
                        &[
                            ("Content-Type", asset.content_type),
                            ("Cache-Control", cache),
                        ],
                    )
                    .await?;
                }
                if !bytes.is_empty() {
                    conn.write_all(bytes).await?;
                }
                return Ok(());
            }
        }

        let (code, resp_body, ctype): (u16, alloc::string::String, &str) =
            routes::dispatch(s, method, path, body, mqtt_ok, heart_tx, heart_rx, bat_pct, bat_mv)
                .await;

        let ctype_header = if ctype == "text/html" {
            "text/html; charset=utf-8"
        } else {
            ctype
        };
        conn.initiate_response(code, Some(reason(code)), &[("Content-Type", ctype_header)])
            .await?;
        if !resp_body.is_empty() {
            conn.write_all(resp_body.as_bytes()).await?;
        }
        Ok(())
    }
}

fn reason(code: u16) -> &'static str {
    match code {
        200 => "OK",
        202 => "Accepted",
        302 => "Found",
        400 => "Bad Request",
        403 => "Forbidden",
        404 => "Not Found",
        405 => "Method Not Allowed",
        503 => "Service Unavailable",
        _ => "OK",
    }
}

/// Dirty-bit SSE hub (PERF-03) — parity with C++ `webEventsTick` + connect snapshot.
async fn run_sse_hub<T, const N: usize>(
    conn: &mut Connection<'_, T, N>,
    s: &HttpShared,
) -> Result<(), ()>
where
    T: Read + Write + TcpSplit,
{
    use chaya_pure::constants::{
        SSE_ALL, SSE_CHAYA, SSE_DEVICE, SSE_MQTT, SSE_OTA, SSE_WIFI,
    };
    use chaya_pure::web::{sse_tick_mask_for_ap_mode, sse_tick_select_bits};
    use crate::mqtt::MqttConfig;
    use crate::sse_dirty::{sse_consume, sse_mark};
    use crate::web::routes::wifi_status_json;
    use embassy_time::{Duration, Instant, Timer};

    const KEEPALIVE_MS: u32 = 8_000;
    const TICK_MS: u64 = 250;
    const MAX_SECS: u64 = 30 * 60;

    // Initial snapshot for the new client (C++ `onEsConnect`).
    sse_mark(SSE_ALL);

    let started = Instant::now();
    let mut last_work_ms: u32 = 0;

    loop {
        if started.elapsed() >= Duration::from_secs(MAX_SECS) {
            break;
        }

        let now_ms = (Instant::now().as_millis() & 0xffff_ffff) as u32;
        let pending = sse_consume();
        let (selected, keepalive) =
            sse_tick_select_bits(pending, now_ms, last_work_ms, KEEPALIVE_MS);
        let work_bits = sse_tick_mask_for_ap_mode(selected, s.ap_mode);
        if work_bits == 0 {
            Timer::after(Duration::from_millis(TICK_MS)).await;
            continue;
        }
        last_work_ms = now_ms;

        if keepalive {
            conn.write_all(sse::sse_keepalive()).await.map_err(|_| ())?;
        }

        let want_chaya = (work_bits & SSE_CHAYA) != 0;
        let want_wifi = (work_bits & SSE_WIFI) != 0;
        let want_mqtt = (work_bits & SSE_MQTT) != 0;
        let want_ota = (work_bits & SSE_OTA) != 0;
        let want_device = (work_bits & SSE_DEVICE) != 0;

        if want_chaya {
            let heart_tx = *s.heart_tx.lock().await;
            let heart_rx = *s.heart_rx.lock().await;
            let mqtt_ok = *s.mqtt_ok.lock().await;
            let (configured, paired) = {
                let mut kv = s.kv.lock().await;
                let cfg = MqttConfig::load(&mut *kv);
                (cfg.broker_configured(), cfg.partner_id.len() == 6)
            };
            let body = json::chaya_json(heart_rx, heart_tx, mqtt_ok, configured, paired);
            let ev = json::sse_typed("chaya", &body);
            if conn.write_all(ev.as_bytes()).await.is_err() {
                sse_mark(SSE_CHAYA);
                return Err(());
            }
        }

        if want_wifi {
            let wifi = wifi_status_json(s).await;
            let ev = json::sse_typed("wifi", &wifi);
            if conn.write_all(ev.as_bytes()).await.is_err() {
                sse_mark(SSE_WIFI);
                return Err(());
            }
        }

        if want_mqtt {
            let mqtt_ok = *s.mqtt_ok.lock().await;
            let configured = {
                let mut kv = s.kv.lock().await;
                MqttConfig::load(&mut *kv).broker_configured()
            };
            if configured {
                let body = json::mqtt_status_json(mqtt_ok);
                let ev = json::sse_typed("mqtt", &body);
                if conn.write_all(ev.as_bytes()).await.is_err() {
                    sse_mark(SSE_MQTT);
                    return Err(());
                }
            }
        }

        if want_ota {
            let st = s.ota_status.lock().await.clone();
            let body = json::ota_status_json(&st);
            let ev = json::sse_typed("ota", &body);
            if conn.write_all(ev.as_bytes()).await.is_err() {
                sse_mark(SSE_OTA);
                return Err(());
            }
        }

        if want_device {
            let bat_pct = *s.bat_pct.lock().await;
            let bat_mv = *s.bat_mv.lock().await;
            // C++ SSE `device` is battery-only; full device_json is for bootstrap.
            let body = json::device_battery_json(bat_mv, bat_pct);
            let ev = json::sse_typed("device", &body);
            if conn.write_all(ev.as_bytes()).await.is_err() {
                sse_mark(SSE_DEVICE);
                return Err(());
            }
        }

        Timer::after(Duration::from_millis(TICK_MS)).await;
    }
    Ok(())
}

/// Run edge-http server (socket-queue mode for embassy-net).
pub async fn run_admin_http(shared: &'static HttpShared) -> ! {
    let buffers = TcpBuffers::<4, 2048, 2048>::new();
    let tcp = Tcp::new(shared.stack, &buffers);
    let addr = SocketAddr::new(IpAddr::V4(Ipv4Addr::UNSPECIFIED), 80);
    let acceptor = tcp.bind(addr).await.expect("http bind");
    let mut server = DefaultServer::new();
    let handler = AdminHandler { shared };
    println!("edge-http admin on :80");
    let _ = SETUP_AP_SSID;
    loop {
        let _ = server
            .run_with_socket_queue::<_, _, 4>(None, &acceptor, &handler)
            .await;
        embassy_time::Timer::after(embassy_time::Duration::from_millis(100)).await;
    }
}
