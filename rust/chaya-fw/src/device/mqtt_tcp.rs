//! MQTT 3.1.1 over TCP, with optional TLS 1.3 when port >= 8883.

use embassy_net::{IpEndpoint, Stack, dns::DnsQueryType, tcp::TcpSocket};
use embassy_time::{Duration, Instant, Timer, with_timeout};
use embedded_io_async::{Read, Write};
use embedded_tls::{
    Aes128GcmSha256, TlsConfig, TlsConnection, TlsContext, UnsecureProvider,
};
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

fn push_remaining_length(mut x: usize, out: &mut heapless::Vec<u8, 384>) {
    loop {
        let mut enc = (x % 128) as u8;
        x /= 128;
        if x > 0 {
            enc |= 0x80;
        }
        let _ = out.push(enc);
        if x == 0 {
            break;
        }
    }
}

fn push_mqtt_string(out: &mut heapless::Vec<u8, 384>, s: &str) {
    let b = s.as_bytes();
    let _ = out.push((b.len() >> 8) as u8);
    let _ = out.push((b.len() & 0xff) as u8);
    let _ = out.extend_from_slice(b);
}

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

fn build_connect(client_id: &str, user: &str, pass: &str) -> heapless::Vec<u8, 384> {
    let mut vh = heapless::Vec::<u8, 384>::new();
    push_mqtt_string(&mut vh, "MQTT");
    let _ = vh.push(0x04);
    let mut flags = 0x02u8;
    if !user.is_empty() {
        flags |= 0x80;
    }
    if !pass.is_empty() {
        flags |= 0x40;
    }
    let _ = vh.push(flags);
    let _ = vh.push(0x00);
    let _ = vh.push(0x3c);
    push_mqtt_string(&mut vh, client_id);
    if !user.is_empty() {
        push_mqtt_string(&mut vh, user);
    }
    if !pass.is_empty() {
        push_mqtt_string(&mut vh, pass);
    }
    let mut pkt = heapless::Vec::<u8, 384>::new();
    let _ = pkt.push(0x10);
    push_remaining_length(vh.len(), &mut pkt);
    let _ = pkt.extend_from_slice(&vh);
    pkt
}

fn build_subscribe(packet_id: u16, topic: &str) -> heapless::Vec<u8, 384> {
    let mut vh = heapless::Vec::<u8, 384>::new();
    let _ = vh.push((packet_id >> 8) as u8);
    let _ = vh.push((packet_id & 0xff) as u8);
    push_mqtt_string(&mut vh, topic);
    let _ = vh.push(0x00);
    let mut pkt = heapless::Vec::<u8, 384>::new();
    let _ = pkt.push(0x82);
    push_remaining_length(vh.len(), &mut pkt);
    let _ = pkt.extend_from_slice(&vh);
    pkt
}

fn build_publish(topic: &str, payload: &str) -> heapless::Vec<u8, 384> {
    let mut vh = heapless::Vec::<u8, 384>::new();
    push_mqtt_string(&mut vh, topic);
    let _ = vh.extend_from_slice(payload.as_bytes());
    let mut pkt = heapless::Vec::<u8, 384>::new();
    let _ = pkt.push(0x30);
    push_remaining_length(vh.len(), &mut pkt);
    let _ = pkt.extend_from_slice(&vh);
    pkt
}

fn parse_i32(s: &[u8]) -> Option<i32> {
    let Ok(t) = core::str::from_utf8(s) else {
        return None;
    };
    let t = t.trim();
    if t.is_empty() {
        return None;
    }
    let mut neg = false;
    let mut rest = t;
    if let Some(r) = t.strip_prefix('-') {
        neg = true;
        rest = r;
    }
    let mut n: i32 = 0;
    for c in rest.bytes() {
        if !c.is_ascii_digit() {
            return None;
        }
        n = n.saturating_mul(10).saturating_add((c - b'0') as i32);
    }
    Some(if neg { -n } else { n })
}

fn consume_packet(buf: &[u8]) -> Option<(usize, Option<i32>)> {
    if buf.len() < 2 {
        return None;
    }
    let kind = buf[0] >> 4;
    let mut multiplier = 1usize;
    let mut value = 0usize;
    let mut i = 1usize;
    loop {
        if i >= buf.len() {
            return None;
        }
        let enc = buf[i];
        i += 1;
        value += (enc & 0x7f) as usize * multiplier;
        multiplier *= 128;
        if enc & 0x80 == 0 {
            break;
        }
        if i > 5 {
            return None;
        }
    }
    let total = i + value;
    if buf.len() < total {
        return None;
    }
    let payload = &buf[i..total];
    let mut published = None;
    if kind == 3 && payload.len() >= 2 {
        let tlen = ((payload[0] as usize) << 8) | payload[1] as usize;
        if payload.len() >= 2 + tlen {
            published = parse_i32(&payload[2 + tlen..]);
        }
    }
    Some((total, published))
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

fn itoa_into(s: &mut heapless::String<16>, mut n: i32) {
    if n < 0 {
        let _ = s.push('-');
        n = -n;
    }
    if n == 0 {
        let _ = s.push('0');
        return;
    }
    let mut digs = heapless::Vec::<u8, 16>::new();
    while n > 0 {
        let _ = digs.push(b'0' + (n % 10) as u8);
        n /= 10;
    }
    for d in digs.iter().rev() {
        let _ = s.push(*d as char);
    }
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
            if tls
                .open(TlsContext::new(
                    &config,
                    UnsecureProvider::new::<Aes128GcmSha256>(&mut rng),
                ))
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
                &mut tls, pub_topic, sub_topic, client_id, user, pass, bump, rx_abs,
            )
            .await
        } else {
            {
                let mut ok = mqtt_ok.lock().await;
                *ok = true;
            }
            mqtt_session_body(
                &mut socket, pub_topic, sub_topic, client_id, user, pass, bump, rx_abs,
            )
            .await
        };
        let _ = up;
        println!("MQTT: session down");
        Timer::after(Duration::from_secs(2)).await;
    }
}
