//! SoftAP DHCP + captive DNS.

use core::net::Ipv4Addr;
use core::str::FromStr;
use embassy_net::Stack;
use embassy_time::{Duration, Timer};
use esp_println::println;

#[embassy_executor::task]
pub async fn run_dhcp(stack: Stack<'static>, gw_ip_addr: &'static str) {
    use core::net::SocketAddrV4;
    use edge_dhcp::{
        io::{self, DEFAULT_SERVER_PORT},
        server::{Server, ServerOptions},
    };
    use edge_nal::UdpBind;
    use edge_nal_embassy::{Udp, UdpBuffers};

    let ip = Ipv4Addr::from_str(gw_ip_addr).unwrap();
    let mut buf = [0u8; 1500];
    let mut gw_buf = [Ipv4Addr::UNSPECIFIED];
    let buffers = UdpBuffers::<3, 1024, 1024, 10>::new();
    let unbound_socket = Udp::new(stack, &buffers);
    let mut bound_socket = unbound_socket
        .bind(core::net::SocketAddr::V4(SocketAddrV4::new(
            Ipv4Addr::UNSPECIFIED,
            DEFAULT_SERVER_PORT,
        )))
        .await
        .unwrap();

    loop {
        let _ = io::server::run(
            &mut Server::<_, 64>::new_with_et(ip),
            &ServerOptions::new(ip, Some(&mut gw_buf)),
            &mut bound_socket,
            &mut buf,
        )
        .await;
        Timer::after(Duration::from_millis(500)).await;
    }
}

#[embassy_executor::task]
pub async fn run_captive_dns(stack: Stack<'static>, gw_ip_addr: &'static str) {
    use core::net::{IpAddr, SocketAddr};
    use edge_captive::io::run;
    use edge_nal_embassy::{Udp, UdpBuffers};

    let ip = Ipv4Addr::from_str(gw_ip_addr).unwrap();
    let mut tx_buf = [0u8; 1500];
    let mut rx_buf = [0u8; 1500];
    let buffers = UdpBuffers::<2, 1500, 1500, 4>::new();
    let udp = Udp::new(stack, &buffers);
    println!("Captive DNS * → {gw_ip_addr}");
    loop {
        let _ = run(
            &udp,
            SocketAddr::new(IpAddr::V4(Ipv4Addr::UNSPECIFIED), 53),
            &mut tx_buf,
            &mut rx_buf,
            ip,
            core::time::Duration::from_secs(60),
        )
        .await;
        Timer::after(Duration::from_millis(200)).await;
    }
}

