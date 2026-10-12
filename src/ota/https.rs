//! Shared HTTPS GET for OTA (TLS via `crate::tls`).

use crate::ota::http_headers::parse_status;
use crate::util::EspRng;
use embassy_net::{dns::DnsQueryType, tcp::TcpSocket, IpEndpoint, Stack};
use embassy_time::Duration;
use embedded_io_async::{Read, Write};

/// DNS + TCP + TLS to `host:443`.
pub async fn connect_https<'a>(
    stack: Stack<'static>,
    host: &str,
    rng: &'a mut EspRng,
    tcp_rx: &'a mut [u8],
    tcp_tx: &'a mut [u8],
    tls_rx: &'a mut [u8],
    tls_tx: &'a mut [u8],
    timeout_secs: u64,
) -> Result<crate::tls::TlsStream<'a, TcpSocket<'a>>, ()> {
    let Ok(addrs) = stack.dns_query(host, DnsQueryType::A).await else {
        return Err(());
    };
    if addrs.is_empty() {
        return Err(());
    }

    let mut socket = TcpSocket::new(stack, tcp_rx, tcp_tx);
    socket.set_timeout(Some(Duration::from_secs(timeout_secs)));
    if socket
        .connect(IpEndpoint::new(addrs[0], 443))
        .await
        .is_err()
    {
        return Err(());
    }

    crate::tls::connect(socket, host, rng, tls_rx, tls_tx).await
}

/// Write an HTTP/1.0 GET request (Connection: close).
pub async fn write_get(
    tls: &mut impl Write,
    host: &str,
    path: &str,
    user_agent: &str,
    accept: &str,
) -> Result<(), ()> {
    let mut req = heapless::String::<384>::new();
    let _ = req.push_str("GET ");
    let _ = req.push_str(path);
    let _ = req.push_str(" HTTP/1.0\r\nHost: ");
    let _ = req.push_str(host);
    let _ = req.push_str("\r\nUser-Agent: ");
    let _ = req.push_str(user_agent);
    let _ = req.push_str("\r\nAccept: ");
    let _ = req.push_str(accept);
    let _ = req.push_str("\r\nConnection: close\r\n\r\n");
    tls.write_all(req.as_bytes()).await.map_err(|_| ())?;
    let _ = tls.flush().await;
    Ok(())
}

/// Read until `\r\n\r\n` into `hdr`; returns header length including the blank line.
pub async fn read_headers(tls: &mut impl Read, hdr: &mut [u8]) -> Result<usize, ()> {
    let mut hdr_len = 0usize;
    loop {
        if hdr_len >= hdr.len() {
            return Err(());
        }
        match tls.read(&mut hdr[hdr_len..hdr_len + 1]).await {
            Ok(0) => return Err(()),
            Ok(n) => {
                hdr_len += n;
                if hdr_len >= 4 && &hdr[hdr_len - 4..hdr_len] == b"\r\n\r\n" {
                    return Ok(hdr_len);
                }
            }
            Err(_) => return Err(()),
        }
    }
}

/// HTTPS GET into `body_buf`. Returns `(status, header_end, total)` where the body is
/// `body_buf[header_end..total]`.
pub async fn get_buffered(
    stack: Stack<'static>,
    rng: &mut EspRng,
    host: &str,
    path: &str,
    user_agent: &str,
    accept: &str,
    body_buf: &mut [u8],
) -> Result<(u16, usize, usize), ()> {
    let mut tcp_rx = [0u8; 4096];
    let mut tcp_tx = [0u8; 2048];
    let mut tls_rx = [0u8; 16384];
    let mut tls_tx = [0u8; 4096];
    let mut tls = connect_https(
        stack, host, rng, &mut tcp_rx, &mut tcp_tx, &mut tls_rx, &mut tls_tx, 30,
    )
    .await?;

    write_get(&mut tls, host, path, user_agent, accept).await?;

    let mut total = 0usize;
    loop {
        if total >= body_buf.len() {
            break;
        }
        match tls.read(&mut body_buf[total..]).await {
            Ok(0) => break,
            Ok(n) => total += n,
            Err(_) => break,
        }
    }

    let header_end = body_buf[..total]
        .windows(4)
        .position(|w| w == b"\r\n\r\n")
        .map(|i| i + 4)
        .unwrap_or(total);
    let headers = core::str::from_utf8(&body_buf[..header_end.min(total)]).unwrap_or("");
    let status = parse_status(headers);
    Ok((status, header_end, total))
}
