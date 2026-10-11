//! GitHub Releases OTA check over TLS 1.3 (flash install still staged).

use embassy_net::{IpEndpoint, Stack, dns::DnsQueryType, tcp::TcpSocket};
use embassy_time::{Duration, Timer};
use embedded_io_async::Write;
use embedded_tls::{
    Aes128GcmSha256, TlsConfig, TlsConnection, TlsContext, UnsecureProvider,
};
use esp_println::println;
use rand_core::{CryptoRng, Error as RandError, RngCore};

const API_HOST: &str = "api.github.com";

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
    pub checking: bool,
    pub flash_ready: bool,
}

fn extract_tag(body: &str, out: &mut heapless::String<32>) -> bool {
    // naive: "tag_name":"v1.2.3"
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

/// HTTPS GET latest release tag. Does not flash yet (SHA-512 install path next).
pub async fn check_latest(
    stack: Stack<'static>,
    rng: &mut EspRng,
    status: &mut OtaStatus,
) -> bool {
    status.checking = true;
    status.last_error.clear();
    let Ok(addrs) = stack.dns_query(API_HOST, DnsQueryType::A).await else {
        let _ = status.last_error.push_str("dns");
        status.checking = false;
        return false;
    };
    if addrs.is_empty() {
        let _ = status.last_error.push_str("dns-empty");
        status.checking = false;
        return false;
    }

    let mut tcp_rx = [0u8; 2048];
    let mut tcp_tx = [0u8; 1024];
    let mut socket = TcpSocket::new(stack, &mut tcp_rx, &mut tcp_tx);
    socket.set_timeout(Some(Duration::from_secs(25)));
    if socket
        .connect(IpEndpoint::new(addrs[0], 443))
        .await
        .is_err()
    {
        let _ = status.last_error.push_str("tcp");
        status.checking = false;
        return false;
    }

    let mut tls_rx = [0u8; 8192];
    let mut tls_tx = [0u8; 4096];
    let config = TlsConfig::new()
        .with_server_name(API_HOST)
        .enable_rsa_signatures();
    let mut tls = TlsConnection::new(socket, &mut tls_rx, &mut tls_tx);
    if tls
        .open(TlsContext::new(
            &config,
            UnsecureProvider::new::<Aes128GcmSha256>(rng),
        ))
        .await
        .is_err()
    {
        let _ = status.last_error.push_str("tls");
        status.checking = false;
        return false;
    }

    let request = b"GET /repos/EyJunge1/chaya2mqtt/releases/latest HTTP/1.0\r\nHost: api.github.com\r\nUser-Agent: chaya2mqtt-rust\r\nAccept: application/vnd.github+json\r\nConnection: close\r\n\r\n";
    if tls.write_all(request).await.is_err() {
        let _ = status.last_error.push_str("write");
        status.checking = false;
        return false;
    }
    let _ = tls.flush().await;

    let mut buf = [0u8; 4096];
    let mut total = 0usize;
    loop {
        match tls.read(&mut buf[total..]).await {
            Ok(0) => break,
            Ok(n) => {
                total += n;
                if total >= buf.len() {
                    break;
                }
            }
            Err(_) => break,
        }
    }
    let text = core::str::from_utf8(&buf[..total]).unwrap_or("");
    // skip headers
    let body = text
        .split("\r\n\r\n")
        .nth(1)
        .unwrap_or(text);
    if extract_tag(body, &mut status.last_tag) {
        println!("OTA: latest {}", status.last_tag.as_str());
        status.flash_ready = false; // install not wired yet
        status.checking = false;
        true
    } else {
        let _ = status.last_error.push_str("parse");
        status.checking = false;
        false
    }
}

/// Placeholder: full SHA-512 verified flash via esp-hal-ota still pending partition wiring.
pub async fn note_install_pending(status: &mut OtaStatus) {
    status.last_error.clear();
    let _ = status.last_error.push_str("flash-todo");
    Timer::after(Duration::from_millis(1)).await;
}
