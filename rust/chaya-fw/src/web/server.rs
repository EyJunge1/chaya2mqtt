//! edge-http admin server (AsyncWebServer replacement).

use core::fmt::{Debug, Display};
use core::net::{IpAddr, Ipv4Addr, SocketAddr};

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
        let (method, path) = {
            let headers = conn.headers()?;
            (headers.method, headers.path)
        };
        let mut path_buf = heapless::String::<128>::new();
        let _ = path_buf.push_str(path.split('?').next().unwrap_or(path));
        let path = path_buf.as_str();

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

        let s = self.shared;
        let mqtt_ok = *s.mqtt_ok.lock().await;
        let heart_tx = *s.heart_tx.lock().await;
        let heart_rx = *s.heart_rx.lock().await;
        let bat_pct = *s.bat_pct.lock().await;
        let bat_mv = *s.bat_mv.lock().await;

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
            for _ in 0..60 {
                let heart_tx = *s.heart_tx.lock().await;
                let heart_rx = *s.heart_rx.lock().await;
                let mqtt_ok = *s.mqtt_ok.lock().await;
                let bat_pct = *s.bat_pct.lock().await;
                let chaya = json::chaya_json(heart_rx, heart_tx, mqtt_ok, mqtt_ok, true);
                let ev = json::sse_typed("chaya", &chaya);
                conn.write_all(ev.as_bytes()).await?;
                let device = json::device_json(s.device_id, s.ap_mode, bat_mv, bat_pct);
                let ev = json::sse_typed("device", &device);
                conn.write_all(ev.as_bytes()).await?;
                conn.write_all(sse::sse_keepalive()).await?;
                embassy_time::Timer::after(embassy_time::Duration::from_secs(5)).await;
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
        404 => "Not Found",
        405 => "Method Not Allowed",
        503 => "Service Unavailable",
        _ => "OK",
    }
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
