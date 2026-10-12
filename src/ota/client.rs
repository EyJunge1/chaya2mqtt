//! GitHub Releases OTA: tag check + SHA-512 verified flash via esp-hal-ota.

use crate::config::flash_shared::FlashCell;
use crate::config::APP_VERSION;
use crate::core::ota::{
    ota_release_download_redirect_url_allowed, ota_release_download_url_allowed,
    ota_release_tag_is_allowed, ota_select_best_release_tag, ota_version_is_newer,
    ota_version_is_rc, OtaDownloadAsset,
};
use crate::ota::http_headers::{parse_content_length, parse_location};
use crate::ota::https;
use crate::util::EspRng;
use embassy_net::Stack;
use esp_println::println;

const API_HOST: &str = "api.github.com";
const DL_HOST: &str = "github.com";
const REPO: &str = "EyJunge1/chaya2mqtt";
const UA: &str = "chaya2mqtt-rust";

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
    let url = url
        .strip_prefix("https://")
        .or_else(|| url.strip_prefix("http://"))?;
    let slash = url.find('/')?;
    Some((&url[..slash], &url[slash..]))
}

/// HTTPS GET latest release tag (`stable` → `/releases/latest`, `beta` → list).
///
/// Sets `flash_ready` only when the remote tag is allowed and newer than `APP_VERSION`.
pub async fn check_latest(stack: Stack<'static>, rng: &mut EspRng, status: &mut OtaStatus) -> bool {
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
    let Ok((status_code, header_end, total)) =
        https::get_buffered(stack, rng, API_HOST, path, UA, "application/vnd.github+json", &mut buf)
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
        let Ok((status_code, header_end, total)) =
            https::get_buffered(stack, rng, host.as_str(), cur_path.as_str(), UA, "*/*", &mut buf)
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
    let Ok((sha_len, _)) =
        download_asset(stack, rng, tag.as_str(), "firmware.sha512", &mut sha_buf).await
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
        let Ok((status_code, header_end, _)) =
            https::get_buffered(stack, rng, host.as_str(), cur_path.as_str(), UA, "*/*", &mut probe)
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
    if !crate::ota::flash::stream_flash_bin(
        stack,
        rng,
        flash,
        final_host.as_str(),
        final_path.as_str(),
        &expect,
        status,
    )
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
