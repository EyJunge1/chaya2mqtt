//! GitHub Releases OTA: tag check + SHA-512 verified flash via esp-hal-ota.

use crate::config::flash_shared::{FlashCell, FlashProxy};
use crate::config::APP_VERSION;
use crate::core::ota::{
    ota_release_download_redirect_url_allowed, ota_release_download_url_allowed, ota_release_tag_is_allowed,
    ota_select_best_release_tag, ota_version_is_newer, ota_version_is_rc, OtaDownloadAsset,
};
use embassy_net::{IpEndpoint, Stack, dns::DnsQueryType, tcp::TcpSocket};
use embassy_time::{Duration, Timer};
use embedded_io_async::Write;
use esp_hal_ota::Ota;
use esp_println::println;
use rand_core::{CryptoRng, Error as RandError, RngCore};
use sha2::{Digest, Sha512};

const API_HOST: &str = "api.github.com";
const DL_HOST: &str = "github.com";
const REPO: &str = "EyJunge1/chaya2mqtt";

pub struct EspRng(pub esp_hal::rng::Rng);

impl RngCore for EspRng {
    fn next_u32(&mut self) -> u32 {
        self.0.random()
    }
    fn next_u64(&mut self) -> u64 {
        ((self.next_u32() as u64) << 32) | self.next_u32() as u64
    }
    fn fill_bytes(&mut self, dest: &mut [u8]) {
        for chunk in dest.chunks_mut(4) {
            let r = self.next_u32().to_le_bytes();
            chunk.copy_from_slice(&r[..chunk.len()]);
        }
    }
    fn try_fill_bytes(&mut self, dest: &mut [u8]) -> Result<(), RandError> {
        self.fill_bytes(dest);
        Ok(())
    }
}
impl CryptoRng for EspRng {}

#[derive(Clone, Debug, Default)]
pub struct OtaStatus {
    pub last_tag: heapless::String<32>,
    pub last_error: heapless::String<48>,
    pub channel: heapless::String<8>,
    pub checking: bool,
    pub installing: bool,
    pub flash_ready: bool,
}

fn extract_tag(body: &str, out: &mut heapless::String<32>) -> bool {
    let Some(i) = body.find("\"tag_name\"") else {
        return false;
    };
    let rest = &body[i..];
    let Some(colon) = rest.find(':') else {
        return false;
    };
    let after = rest[colon + 1..].trim_start();
    let after = after.strip_prefix('"').unwrap_or(after);
    let end = after.find('"').unwrap_or(0);
    if end == 0 || end > 31 {
        return false;
    }
    out.clear();
    let _ = out.push_str(&after[..end]);
    true
}

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

fn parse_location<'a>(headers: &'a str) -> Option<&'a str> {
    for line in headers.lines() {
        if let Some(rest) = line.strip_prefix("Location:") {
            return Some(rest.trim());
        }
        if let Some(rest) = line.strip_prefix("location:") {
            return Some(rest.trim());
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

fn parse_sha512_hex(text: &str, out: &mut [u8; 64]) -> bool {
    let mut hex = heapless::String::<128>::new();
    for b in text.bytes() {
        if b.is_ascii_hexdigit() {
            if hex.push(b as char).is_err() {
                break;
            }
            if hex.len() == 128 {
                break;
            }
        }
    }
    if hex.len() != 128 {
        return false;
    }
    for i in 0..64 {
        let Ok(v) = u8::from_str_radix(&hex[i * 2..i * 2 + 2], 16) else {
            return false;
        };
        out[i] = v;
    }
    true
}

fn split_url(url: &str) -> Option<(&str, &str)> {
    let url = url.strip_prefix("https://").or_else(|| url.strip_prefix("http://"))?;
    let slash = url.find('/')?;
    Some((&url[..slash], &url[slash..]))
}

async fn tls_get(
    stack: Stack<'static>,
    rng: &mut EspRng,
    host: &str,
    path: &str,
    user_agent: &str,
    accept: &str,
    body_buf: &mut [u8],
) -> Result<(u16, usize, usize), ()> {
    // returns (status, header_end, total) where body is body_buf[header_end..total]
    let Ok(addrs) = stack.dns_query(host, DnsQueryType::A).await else {
        return Err(());
    };
    if addrs.is_empty() {
        return Err(());
    }

    let mut tcp_rx = [0u8; 4096];
    let mut tcp_tx = [0u8; 2048];
    let mut socket = TcpSocket::new(stack, &mut tcp_rx, &mut tcp_tx);
    socket.set_timeout(Some(Duration::from_secs(30)));
    if socket
        .connect(IpEndpoint::new(addrs[0], 443))
        .await
        .is_err()
    {
        return Err(());
    }

    let mut tls_rx = [0u8; 16384];
    let mut tls_tx = [0u8; 4096];
    let Ok(mut tls) = crate::tls::connect(socket, host, rng, &mut tls_rx, &mut tls_tx).await else {
        return Err(());
    };

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
    if tls.write_all(req.as_bytes()).await.is_err() {
        return Err(());
    }
    let _ = tls.flush().await;

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

/// HTTPS GET latest release tag (`stable` → `/releases/latest`, `beta` → list).
///
/// Sets `flash_ready` only when the remote tag is allowed and newer than `APP_VERSION`.
pub async fn check_latest(
    stack: Stack<'static>,
    rng: &mut EspRng,
    status: &mut OtaStatus,
) -> bool {
    status.checking = true;
    status.flash_ready = false;
    status.last_error.clear();
    let mut buf = [0u8; 8192];
    let beta = status.channel.as_str() == "beta";
    let path = if beta {
        "/repos/EyJunge1/chaya2mqtt/releases?per_page=20"
    } else {
        "/repos/EyJunge1/chaya2mqtt/releases/latest"
    };
    let Ok((status_code, header_end, total)) = tls_get(
        stack,
        rng,
        API_HOST,
        path,
        "chaya2mqtt-rust",
        "application/vnd.github+json",
        &mut buf,
    )
    .await
    else {
        let _ = status.last_error.push_str("http");
        status.checking = false;
        return false;
    };
    if status_code != 200 {
        let _ = status.last_error.push_str("status");
        status.checking = false;
        return false;
    }
    let body = core::str::from_utf8(&buf[header_end..total]).unwrap_or("");

    status.last_tag.clear();
    let tag_ok = if beta {
        if let Some(tag) = ota_select_best_release_tag(body, true) {
            let _ = status.last_tag.push_str(tag);
            true
        } else {
            false
        }
    } else {
        extract_tag(body, &mut status.last_tag)
    };

    if !tag_ok {
        let _ = status.last_error.push_str("parse");
        status.checking = false;
        return false;
    }
    let tag = status.last_tag.as_str();
    if !ota_release_tag_is_allowed(tag) || (!beta && ota_version_is_rc(tag)) {
        status.last_tag.clear();
        let _ = status.last_error.push_str("tag");
        status.checking = false;
        return false;
    }

    println!(
        "OTA: {} → {} (local={})",
        if beta { "beta" } else { "stable" },
        tag,
        APP_VERSION
    );

    if ota_version_is_newer(tag, APP_VERSION) {
        status.flash_ready = true;
        status.checking = false;
        true
    } else {
        // Successful check, but no upgrade available (parity with ParsedNoUpgrade).
        status.last_tag.clear();
        status.flash_ready = false;
        status.checking = false;
        true
    }
}

async fn download_asset(
    stack: Stack<'static>,
    rng: &mut EspRng,
    tag: &str,
    asset: &str,
    body_buf: &mut [u8],
) -> Result<(usize, Option<u32>), ()> {
    // returns (body_len in body_buf, content_length if known) — for small assets (sha512)
    let kind = if asset == "firmware.bin" {
        OtaDownloadAsset::Firmware
    } else if asset == "firmware.sha512" {
        OtaDownloadAsset::Sha512
    } else {
        return Err(());
    };
    let mut full = heapless::String::<192>::new();
    let _ = full.push_str("https://");
    let _ = full.push_str(DL_HOST);
    let _ = full.push_str("/");
    let _ = full.push_str(REPO);
    let _ = full.push_str("/releases/download/");
    let _ = full.push_str(tag);
    let _ = full.push('/');
    let _ = full.push_str(asset);
    if !ota_release_download_url_allowed(full.as_str(), kind) {
        return Err(());
    }

    let mut path = heapless::String::<160>::new();
    let _ = path.push_str("/");
    let _ = path.push_str(REPO);
    let _ = path.push_str("/releases/download/");
    let _ = path.push_str(tag);
    let _ = path.push('/');
    let _ = path.push_str(asset);

    let mut host = heapless::String::<64>::new();
    let _ = host.push_str(DL_HOST);
    let mut cur_path = path;

    for _ in 0..3 {
        let mut buf = [0u8; 8192];
        let Ok((status_code, header_end, total)) = tls_get(
            stack,
            rng,
            host.as_str(),
            cur_path.as_str(),
            "chaya2mqtt-rust",
            "*/*",
            &mut buf,
        )
        .await
        else {
            return Err(());
        };
        let headers = core::str::from_utf8(&buf[..header_end]).unwrap_or("");
        if status_code == 301 || status_code == 302 || status_code == 307 || status_code == 308 {
            let Some(loc) = parse_location(headers) else {
                return Err(());
            };
            if !ota_release_download_redirect_url_allowed(loc) {
                return Err(());
            }
            let Some((h, p)) = split_url(loc) else {
                return Err(());
            };
            host.clear();
            let _ = host.push_str(h);
            cur_path.clear();
            let _ = cur_path.push_str(p);
            continue;
        }
        if status_code != 200 {
            return Err(());
        }
        let cl = parse_content_length(headers);
        let body = &buf[header_end..total];
        if body.len() > body_buf.len() {
            return Err(());
        }
        body_buf[..body.len()].copy_from_slice(body);
        return Ok((body.len(), cl));
    }
    Err(())
}

/// Stream firmware.bin into OTA partition; verify SHA-512 from firmware.sha512.
pub async fn install_latest(
    stack: Stack<'static>,
    rng: &mut EspRng,
    flash: &'static FlashCell,
    status: &mut OtaStatus,
) -> bool {
    status.installing = true;
    status.last_error.clear();
    if !status.flash_ready || status.last_tag.is_empty() {
        let _ = status.last_error.push_str("not-ready");
        status.installing = false;
        return false;
    }
    let tag = status.last_tag.clone();
    if !ota_release_tag_is_allowed(tag.as_str()) {
        let _ = status.last_error.push_str("tag");
        status.installing = false;
        return false;
    }

    // 1) SHA-512 file
    let mut sha_buf = [0u8; 256];
    let Ok((sha_len, _)) = download_asset(stack, rng, tag.as_str(), "firmware.sha512", &mut sha_buf).await
    else {
        let _ = status.last_error.push_str("sha-dl");
        status.installing = false;
        return false;
    };
    let sha_text = core::str::from_utf8(&sha_buf[..sha_len]).unwrap_or("");
    let mut expect = [0u8; 64];
    if !parse_sha512_hex(sha_text, &mut expect) {
        let _ = status.last_error.push_str("sha-parse");
        status.installing = false;
        return false;
    }

    // 2) Resolve firmware.bin URL (follow redirects) then stream
    let mut bin_url = heapless::String::<192>::new();
    let _ = bin_url.push_str("https://");
    let _ = bin_url.push_str(DL_HOST);
    let _ = bin_url.push_str("/");
    let _ = bin_url.push_str(REPO);
    let _ = bin_url.push_str("/releases/download/");
    let _ = bin_url.push_str(tag.as_str());
    let _ = bin_url.push_str("/firmware.bin");
    if !ota_release_download_url_allowed(bin_url.as_str(), OtaDownloadAsset::Firmware) {
        let _ = status.last_error.push_str("url");
        status.installing = false;
        return false;
    }

    let mut path = heapless::String::<160>::new();
    let _ = path.push_str("/");
    let _ = path.push_str(REPO);
    let _ = path.push_str("/releases/download/");
    let _ = path.push_str(tag.as_str());
    let _ = path.push_str("/firmware.bin");
    let mut host = heapless::String::<64>::new();
    let _ = host.push_str(DL_HOST);
    let mut cur_path = path;

    let mut final_host = host.clone();
    let mut final_path = cur_path.clone();
    for _ in 0..3 {
        let mut probe = [0u8; 2048];
        let Ok((status_code, header_end, _)) = tls_get(
            stack,
            rng,
            host.as_str(),
            cur_path.as_str(),
            "chaya2mqtt-rust",
            "*/*",
            &mut probe,
        )
        .await
        else {
            let _ = status.last_error.push_str("bin-probe");
            status.installing = false;
            return false;
        };
        let headers = core::str::from_utf8(&probe[..header_end]).unwrap_or("");
        if status_code == 301 || status_code == 302 || status_code == 307 || status_code == 308 {
            let Some(loc) = parse_location(headers) else {
                let _ = status.last_error.push_str("redir");
                status.installing = false;
                return false;
            };
            if !ota_release_download_redirect_url_allowed(loc) {
                let _ = status.last_error.push_str("redir-deny");
                status.installing = false;
                return false;
            }
            let Some((h, p)) = split_url(loc) else {
                let _ = status.last_error.push_str("redir-url");
                status.installing = false;
                return false;
            };
            host.clear();
            let _ = host.push_str(h);
            cur_path.clear();
            let _ = cur_path.push_str(p);
            continue;
        }
        // Got a non-redirect — but we consumed the body in probe buffer (too small for bin).
        // Re-fetch for real stream. Cache resolved URL.
        final_host = host.clone();
        final_path = cur_path.clone();
        break;
    }

    // Real stream download + flash
    if !crate::ota::flash::stream_flash_bin(stack, rng, flash, final_host.as_str(), final_path.as_str(), &expect, status)
        .await
    {
        status.installing = false;
        return false;
    }
    status.flash_ready = false;
    status.installing = false;
    println!("OTA: install ok, reboot required");
    true
}


