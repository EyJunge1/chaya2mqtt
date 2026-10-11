//! SNTP via `sntpc` + thin Embassy UDP adapter (avoids `sntpc-net-embassy` IPv6 mismatch).

use core::net::{IpAddr, SocketAddr};

use embassy_net::{
    Stack,
    dns::DnsQueryType,
    udp::{PacketMetadata, UdpSocket},
    IpAddress, IpEndpoint,
};
use embassy_time::Instant;
use esp_println::println;
use sntpc::{Error as NtpError, NtpContext, NtpTimestampGenerator, NtpUdpSocket, Result as NtpResult, get_time};

const NTP_HOST: &str = "time.cloudflare.com";
const NTP_PORT: u16 = 123;
/// Pivot near 2024-01-01 so era reconstruction works before the first sync.
const PIVOT_UNIX: u64 = 1_704_067_200;

#[derive(Clone, Copy, Debug, Default)]
pub struct WallClock {
    pub synced_at: Option<Instant>,
    pub unix_at_sync: u64,
}

impl WallClock {
    pub fn mark(&mut self, unix: u64) {
        self.synced_at = Some(Instant::now());
        self.unix_at_sync = unix;
    }

    pub fn synced(&self) -> bool {
        self.synced_at.is_some()
    }

    pub fn unix_now(&self) -> Option<u64> {
        let at = self.synced_at?;
        let elapsed = Instant::now().duration_since(at).as_secs();
        Some(self.unix_at_sync.saturating_add(elapsed))
    }

    pub fn utc_hour(&self) -> Option<u8> {
        let u = self.unix_now()?;
        Some(((u / 3600) % 24) as u8)
    }
}

#[derive(Clone, Copy)]
struct EmbassyTimestampGen {
    captured: Instant,
    base_unix: u64,
}

impl EmbassyTimestampGen {
    fn with_base(base_unix: u64) -> Self {
        Self {
            captured: Instant::now(),
            base_unix,
        }
    }
}

impl NtpTimestampGenerator for EmbassyTimestampGen {
    fn init(&mut self) {
        self.captured = Instant::now();
    }

    fn timestamp_sec(&self) -> u64 {
        let elapsed = Instant::now().duration_since(self.captured).as_secs();
        self.base_unix.saturating_add(elapsed)
    }

    fn timestamp_subsec_micros(&self) -> u32 {
        Instant::now().duration_since(self.captured).as_micros() as u32 % 1_000_000
    }
}

struct EmbassyUdp<'a> {
    socket: UdpSocket<'a>,
}

impl NtpUdpSocket for EmbassyUdp<'_> {
    async fn send_to(&self, buf: &[u8], addr: SocketAddr) -> NtpResult<usize> {
        let IpAddr::V4(v4) = addr.ip() else {
            return Err(NtpError::Network);
        };
        let endpoint = IpEndpoint::new(IpAddress::Ipv4(v4), addr.port());
        self.socket
            .send_to(buf, endpoint)
            .await
            .map(|_| buf.len())
            .map_err(|_| NtpError::Network)
    }

    async fn recv_from(&self, buf: &mut [u8]) -> NtpResult<(usize, SocketAddr)> {
        let (len, meta) = self
            .socket
            .recv_from(buf)
            .await
            .map_err(|_| NtpError::Network)?;
        let IpAddress::Ipv4(v4) = meta.endpoint.addr else {
            return Err(NtpError::Network);
        };
        Ok((len, SocketAddr::new(IpAddr::V4(v4), meta.endpoint.port)))
    }
}

pub async fn sync_once(stack: Stack<'static>, clock: &mut WallClock) -> bool {
    let Ok(addrs) = stack.dns_query(NTP_HOST, DnsQueryType::A).await else {
        println!("NTP: dns fail");
        return false;
    };
    let Some(ip) = addrs.first().copied() else {
        return false;
    };
    let IpAddr::V4(v4) = IpAddr::from(ip) else {
        println!("NTP: no v4");
        return false;
    };

    let mut rx_meta = [PacketMetadata::EMPTY; 2];
    let mut rx_buf = [0u8; 256];
    let mut tx_meta = [PacketMetadata::EMPTY; 2];
    let mut tx_buf = [0u8; 256];
    let mut socket = UdpSocket::new(stack, &mut rx_meta, &mut rx_buf, &mut tx_meta, &mut tx_buf);
    if socket.bind(0).is_err() {
        return false;
    }

    let wrapper = EmbassyUdp { socket };
    let base = clock.unix_now().unwrap_or(PIVOT_UNIX);
    let ctx = NtpContext::new(EmbassyTimestampGen::with_base(base));
    let addr = SocketAddr::new(IpAddr::V4(v4), NTP_PORT);

    match get_time(addr, &wrapper, ctx).await {
        Ok(res) => {
            let unix = res.sec();
            clock.mark(unix);
            println!("NTP: ok unix={unix}");
            true
        }
        Err(e) => {
            println!("NTP: err {e:?}");
            false
        }
    }
}
