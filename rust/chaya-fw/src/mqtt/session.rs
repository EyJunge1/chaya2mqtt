//! MQTT 3.1.1 over TCP, with optional TLS 1.3 when port >= 8883.

use embassy_net::{IpEndpoint, Stack, dns::DnsQueryType, tcp::TcpSocket};
use embassy_time::{Duration, Instant, Timer, with_timeout};
use embedded_io_async::{Read, Write};
use crate::tls::HostPinProvider;
use chaya_pure::audio::AudioKind;
use embedded_tls::{TlsConfig, TlsConnection, TlsContext};
use esp_println::println;
use rand_core::{CryptoRng, Error as RandError, RngCore};

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


use crate::mqtt::proto::{
    build_connect, build_publish, build_subscribe, consume_packet, itoa_into, parse_i32,
};

async fn resolve(stack: Stack<'static>, host: &str) -> Option<embassy_net::IpAddress> {
    match stack.dns_query(host, DnsQueryType::A).await {
        Ok(addrs) if !addrs.is_empty() => Some(addrs[0]),
        Ok(_) => {
            println!("MQTT: DNS empty for {host}");
            None
        }
        Err(e) => {
            println!("MQTT: DNS err {e:?}");
            None
        }
    }
}

async fn read_exact<S: Read>(stream: &mut S, buf: &mut [u8]) -> bool {
    let mut got = 0;
    while got < buf.len() {
        match stream.read(&mut buf[got..]).await {
            Ok(0) => return false,
            Ok(n) => got += n,
            Err(_) => return false,
        }
    }
    true
}

async fn mqtt_session_body<S: Read + Write>(
    stream: &mut S,
    pub_topic: &str,
    sub_topic: &str,
    client_id: &str,
    user: &str,
    pass: &str,
    bump: &embassy_sync::signal::Signal<
        embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex,
        i32,
    >,
    rx_abs: &embassy_sync::mutex::Mutex<
        embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex,
        i32,
    >,
    rx_beep: &embassy_sync::signal::Signal<
        embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex,
        AudioKind,
    >,
    heart_dirty: &embassy_sync::signal::Signal<
        embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex,
        (),
    >,
    mqtt_up: &embassy_sync::signal::Signal<
        embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex,
        (),
    >,
) -> bool {
    let pkt = build_connect(client_id, user, pass);
    if stream.write_all(&pkt).await.is_err() {
        return false;
    }
    let _ = stream.flush().await;
    let mut ack = [0u8; 4];
    if !read_exact(stream, &mut ack).await || ack[0] != 0x20 || ack[3] != 0x00 {
        println!("MQTT: CONNACK fail {ack:?}");
        return false;
    }
    if !sub_topic.is_empty() {
        let sub_pkt = build_subscribe(1, sub_topic);
        let _ = stream.write_all(&sub_pkt).await;
        let _ = stream.flush().await;
    }
    println!("MQTT: session up");
    mqtt_up.signal(());

    let mut acc = [0u8; 768];
    let mut acc_len = 0usize;
    let mut ping_at = Instant::now() + Duration::from_secs(45);

    loop {
        if let Some(abs) = bump.try_take() {
            let mut payload = heapless::String::<16>::new();
            itoa_into(&mut payload, abs);
            let pub_pkt = build_publish(pub_topic, payload.as_str());
            if stream.write_all(&pub_pkt).await.is_err() {
                return true;
            }
            let _ = stream.flush().await;
        }
        if Instant::now() >= ping_at {
            if stream.write_all(&[0xc0, 0x00]).await.is_err() {
                return true;
            }
            let _ = stream.flush().await;
            ping_at = Instant::now() + Duration::from_secs(45);
        }
        match with_timeout(Duration::from_millis(200), stream.read(&mut acc[acc_len..])).await {
            Ok(Ok(0)) => return true,
            Ok(Ok(n)) => {
                acc_len += n;
                while let Some((used, val)) = consume_packet(&acc[..acc_len]) {
                    if let Some(v) = val {
                        let mut g = rx_abs.lock().await;
                        if v > *g {
                            *g = v;
                            println!("MQTT: rx heart={v}");
                            rx_beep.signal(AudioKind::Rx);
                            heart_dirty.signal(());
                        }
                    }
                    acc.copy_within(used..acc_len, 0);
                    acc_len -= used;
                }
            }
            Ok(Err(_)) => return true,
            Err(_) => {}
        }
    }
}

/// Long-lived session; uses TLS when `port >= 8883`.
pub async fn session_loop(
    stack: Stack<'static>,
    host: &'static str,
    port: u16,
    client_id: &'static str,
    user: &'static str,
    pass: &'static str,
    pub_topic: &'static str,
    sub_topic: &'static str,
    bump: &'static embassy_sync::signal::Signal<
        embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex,
        i32,
    >,
    rx_abs: &'static embassy_sync::mutex::Mutex<
        embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex,
        i32,
    >,
    mqtt_ok: &'static embassy_sync::mutex::Mutex<
        embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex,
        bool,
    >,
    rx_beep: &'static embassy_sync::signal::Signal<
        embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex,
        AudioKind,
    >,
    heart_dirty: &'static embassy_sync::signal::Signal<
        embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex,
        (),
    >,
    mqtt_up: &'static embassy_sync::signal::Signal<
        embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex,
        (),
    >,
    mut rng: EspRng,
) -> ! {
    let use_tls = port >= 8883;
    let mut rx = [0u8; 2048];
    let mut tx = [0u8; 1536];
    loop {
        {
            let mut ok = mqtt_ok.lock().await;
            *ok = false;
        }
        let Some(addr) = resolve(stack, host).await else {
            Timer::after(Duration::from_secs(5)).await;
            continue;
        };
        let mut socket = TcpSocket::new(stack, &mut rx, &mut tx);
        socket.set_timeout(Some(Duration::from_secs(90)));
        println!(
            "MQTT: connect {host}:{port} ({})",
            if use_tls { "TLS" } else { "TCP" }
        );
        if socket.connect(IpEndpoint::new(addr, port)).await.is_err() {
            Timer::after(Duration::from_secs(3)).await;
            continue;
        }

        let up = if use_tls {
            let mut tls_rx = [0u8; 8192];
            let mut tls_tx = [0u8; 4096];
            let config = TlsConfig::new()
                .with_server_name(host)
                .enable_rsa_signatures();
            let mut tls = TlsConnection::new(socket, &mut tls_rx, &mut tls_tx);
            let mut provider = HostPinProvider::new(&mut rng);
            if tls
                .open(TlsContext::new(&config, &mut provider))
                .await
                .is_err()
            {
                println!("MQTT: TLS handshake failed");
                Timer::after(Duration::from_secs(3)).await;
                continue;
            }
            {
                let mut ok = mqtt_ok.lock().await;
                *ok = true;
            }
            mqtt_session_body(
                &mut tls,
                pub_topic,
                sub_topic,
                client_id,
                user,
                pass,
                bump,
                rx_abs,
                rx_beep,
                heart_dirty,
                mqtt_up,
            )
            .await
        } else {
            {
                let mut ok = mqtt_ok.lock().await;
                *ok = true;
            }
            mqtt_session_body(
                &mut socket,
                pub_topic,
                sub_topic,
                client_id,
                user,
                pass,
                bump,
                rx_abs,
                rx_beep,
                heart_dirty,
                mqtt_up,
            )
            .await
        };
        let _ = up;
        println!("MQTT: session down");
        Timer::after(Duration::from_secs(2)).await;
    }
}
