//! mDNS responder via `edge-mdns` (`chaya2mqtt-<id>.local`).

use core::net::{Ipv4Addr, Ipv6Addr, SocketAddr, SocketAddrV4};

use edge_mdns::buf::VecBufAccess;
use edge_mdns::domain::base::Ttl;
use edge_mdns::host::Host;
use edge_mdns::io::{self as mdns_io, Mdns};
use edge_mdns::HostAnswersMdnsHandler;
use edge_nal::UdpSplit;
use edge_nal_embassy::{Udp, UdpBuffers};
use embassy_net::Stack;
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::signal::Signal;
use crate::util::EspRng10;
use esp_hal::rng::Rng;
use esp_println::println;
use static_cell::StaticCell;

type Cs = CriticalSectionRawMutex;

/// Run mDNS for `hostname.local` answering A records for the current STA IPv4.
pub async fn run_mdns(stack: Stack<'static>, hostname: &'static str) -> ! {
    let ipv4 = loop {
        if let Some(cfg) = stack.config_v4() {
            break cfg.address.address();
        }
        embassy_time::Timer::after(embassy_time::Duration::from_secs(1)).await;
    };

    println!("mDNS: {hostname}.local → {ipv4}");

    let buffers = UdpBuffers::<2, 1500, 1500, 2>::new();
    let udp = Udp::new(stack, &buffers);
    let mut socket = mdns_io::bind(
        &udp,
        SocketAddr::V4(SocketAddrV4::new(Ipv4Addr::UNSPECIFIED, 5353)),
        Some(ipv4),
        None,
    )
    .await
    .expect("mdns bind");

    let (recv, send) = socket.split();

    static RECV_BUF: StaticCell<VecBufAccess<Cs, 1500>> = StaticCell::new();
    static SEND_BUF: StaticCell<VecBufAccess<Cs, 1500>> = StaticCell::new();
    let recv_buf: &'static VecBufAccess<Cs, 1500> = RECV_BUF.init(VecBufAccess::new());
    let send_buf: &'static VecBufAccess<Cs, 1500> = SEND_BUF.init(VecBufAccess::new());

    static BROADCAST: StaticCell<Signal<Cs, ()>> = StaticCell::new();
    let broadcast_signal = BROADCAST.init(Signal::new());

    let mdns = Mdns::<_, _, _, _, _, Cs>::new(
        Some(ipv4),
        None,
        recv,
        send,
        recv_buf,
        send_buf,
        EspRng10(Rng::new()),
        broadcast_signal,
    );

    let host = Host {
        hostname,
        ipv4,
        ipv6: Ipv6Addr::UNSPECIFIED,
        ttl: Ttl::from_secs(120),
    };

    loop {
        let handler = HostAnswersMdnsHandler::new(&host);
        if mdns.run(handler).await.is_err() {
            embassy_time::Timer::after(embassy_time::Duration::from_millis(200)).await;
        }
    }
}
