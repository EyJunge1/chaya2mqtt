//! OTA binary stream + SHA-512 verified flash.

use crate::tls::HostPinProvider;
use super::{EspRng, OtaStatus};
use crate::config::flash_shared::{FlashCell, FlashProxy};
use embassy_net::{IpEndpoint, Stack, dns::DnsQueryType, tcp::TcpSocket};
use embassy_time::{Duration, Timer};
use embedded_io_async::Write;
use embedded_tls::{TlsConfig, TlsConnection, TlsContext};
use esp_hal_ota::Ota;
use esp_println::println;
use sha2::{Digest, Sha512};

fn header_eq_ignore_case<'a>(line: &'a str, name: &str) -> Option<&'a str> {
    let bytes = line.as_bytes();
    let n = name.as_bytes();
    if bytes.len() < n.len() + 1 {
        return None;
    }
    for i in 0..n.len() {
        let a = bytes[i].to_ascii_lowercase();
        let b = n[i].to_ascii_lowercase();
        if a != b {
            return None;
        }
    }
    if bytes[n.len()] != b':' {
        return None;
    }
    Some(line[n.len() + 1..].trim())
}

fn parse_content_length(headers: &str) -> Option<u32> {
    for line in headers.lines() {
        if let Some(rest) = header_eq_ignore_case(line, "content-length") {
            return rest.parse().ok();
        }
    }
    None
}
fn parse_status(headers: &str) -> u16 {
    let line = headers.lines().next().unwrap_or("");
    let mut parts = line.split_whitespace();
    let _ = parts.next();
    parts
        .next()
        .and_then(|s| s.parse().ok())
        .unwrap_or(0)
}

pub async fn stream_flash_bin(
    stack: Stack<'static>,
    rng: &mut EspRng,
    flash: &'static FlashCell,
    host: &str,
    path: &str,
    expect_sha: &[u8; 64],
    status: &mut OtaStatus,
) -> bool {
    let Ok(addrs) = stack.dns_query(host, DnsQueryType::A).await else {
        let _ = status.last_error.push_str("dns");
        return false;
    };
    if addrs.is_empty() {
        let _ = status.last_error.push_str("dns-empty");
        return false;
    }

    let mut tcp_rx = [0u8; 4096];
    let mut tcp_tx = [0u8; 1024];
    let mut socket = TcpSocket::new(stack, &mut tcp_rx, &mut tcp_tx);
    socket.set_timeout(Some(Duration::from_secs(60)));
    if socket
        .connect(IpEndpoint::new(addrs[0], 443))
        .await
        .is_err()
    {
        let _ = status.last_error.push_str("tcp");
        return false;
    }

    let mut tls_rx = [0u8; 16384];
    let mut tls_tx = [0u8; 2048];
    let config = TlsConfig::new()
        .with_server_name(host)
        .enable_rsa_signatures();
    let mut provider = HostPinProvider::new(rng);
    let mut tls = TlsConnection::new(socket, &mut tls_rx, &mut tls_tx);
    if tls
        .open(TlsContext::new(&config, &mut provider))
        .await
        .is_err()
    {
        let _ = status.last_error.push_str("tls");
        return false;
    }

    let mut req = heapless::String::<320>::new();
    let _ = req.push_str("GET ");
    let _ = req.push_str(path);
    let _ = req.push_str(" HTTP/1.0\r\nHost: ");
    let _ = req.push_str(host);
    let _ = req.push_str("\r\nUser-Agent: chaya2mqtt-rust\r\nAccept: */*\r\nConnection: close\r\n\r\n");
    if tls.write_all(req.as_bytes()).await.is_err() {
        let _ = status.last_error.push_str("write");
        return false;
    }
    let _ = tls.flush().await;

    // Read headers
    let mut hdr = [0u8; 1024];
    let mut hdr_len = 0usize;
    loop {
        if hdr_len >= hdr.len() {
            let _ = status.last_error.push_str("hdr");
            return false;
        }
        match tls.read(&mut hdr[hdr_len..hdr_len + 1]).await {
            Ok(0) => {
                let _ = status.last_error.push_str("hdr-eof");
                return false;
            }
            Ok(n) => {
                hdr_len += n;
                if hdr_len >= 4 && &hdr[hdr_len - 4..hdr_len] == b"\r\n\r\n" {
                    break;
                }
            }
            Err(_) => {
                let _ = status.last_error.push_str("hdr-err");
                return false;
            }
        }
    }
    let headers = core::str::from_utf8(&hdr[..hdr_len]).unwrap_or("");
    let status_code = parse_status(headers);
    if status_code != 200 {
        let _ = status.last_error.push_str("bin-status");
        return false;
    }
    let Some(size) = parse_content_length(headers) else {
        let _ = status.last_error.push_str("no-clen");
        return false;
    };
    if size == 0 || size > 0x180000 {
        let _ = status.last_error.push_str("size");
        return false;
    }

    let mut ota = match Ota::new(FlashProxy(flash)) {
        Ok(o) => o,
        Err(_) => {
            let _ = status.last_error.push_str("ota-init");
            return false;
        }
    };
    // CRC target dummy — esp-hal-ota CRC may be stub; SHA-512 is authoritative.
    // After write: ota_resume with last_crc==target so flush accepts.
    if ota.ota_begin(size, 0).is_err() {
        let _ = status.last_error.push_str("ota-begin");
        return false;
    }

    let mut hasher = Sha512::new();
    let mut chunk = [0u8; 4096];
    let mut remaining = size;
    while remaining > 0 {
        let want = (remaining as usize).min(chunk.len());
        let mut got = 0usize;
        while got < want {
            match tls.read(&mut chunk[got..want]).await {
                Ok(0) => {
                    let _ = status.last_error.push_str("bin-eof");
                    return false;
                }
                Ok(n) => got += n,
                Err(_) => {
                    let _ = status.last_error.push_str("bin-read");
                    return false;
                }
            }
        }
        hasher.update(&chunk[..got]);
        if ota.ota_write_chunk(&chunk[..got]).is_err() {
            let _ = status.last_error.push_str("flash-w");
            return false;
        }
        remaining -= got as u32;
    }

    let digest = hasher.finalize();
    if digest.as_slice() != expect_sha.as_slice() {
        let _ = status.last_error.push_str("sha-mismatch");
        return false;
    }

    let last_crc = ota
        .get_progress_details()
        .map(|d| d.last_crc)
        .unwrap_or(0);
    if ota
        .ota_resume(size, 0, last_crc, last_crc, false)
        .is_err()
    {
        let _ = status.last_error.push_str("ota-resume");
        return false;
    }
    if ota.ota_flush(false, false).is_err() {
        let _ = status.last_error.push_str("ota-flush");
        return false;
    }
    Timer::after(Duration::from_millis(50)).await;
    true
}