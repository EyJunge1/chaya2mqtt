//! OTA binary stream + SHA-512 verified flash.

use super::https;
use super::OtaStatus;
use crate::config::flash_shared::{FlashCell, FlashProxy};
use crate::ota::http_headers::{parse_content_length, parse_status};
use crate::util::EspRng;
use embassy_net::Stack;
use embassy_time::{Duration, Timer};
use esp_hal_ota::Ota;
use sha2::{Digest, Sha512};

pub async fn stream_flash_bin(
    stack: Stack<'static>,
    rng: &mut EspRng,
    flash: &'static FlashCell,
    host: &str,
    path: &str,
    expect_sha: &[u8; 64],
    status: &mut OtaStatus,
) -> bool {
    let mut tcp_rx = [0u8; 4096];
    let mut tcp_tx = [0u8; 1024];
    let mut tls_rx = [0u8; 16384];
    let mut tls_tx = [0u8; 2048];
    let Ok(mut tls) = https::connect_https(
        stack, host, rng, &mut tcp_rx, &mut tcp_tx, &mut tls_rx, &mut tls_tx, 60,
    )
    .await
    else {
        let _ = status.last_error.push_str("tls");
        return false;
    };

    if https::write_get(&mut tls, host, path, "chaya2mqtt-rust", "*/*")
        .await
        .is_err()
    {
        let _ = status.last_error.push_str("write");
        return false;
    }

    let mut hdr = [0u8; 1024];
    let Ok(hdr_len) = https::read_headers(&mut tls, &mut hdr).await else {
        let _ = status.last_error.push_str("hdr");
        return false;
    };
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

    let last_crc = ota.get_progress_details().map(|d| d.last_crc).unwrap_or(0);
    if ota.ota_resume(size, 0, last_crc, last_crc, false).is_err() {
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
