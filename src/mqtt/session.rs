//! MQTT 3.1.1 over TCP, with optional TLS 1.3 when port >= 8883.

use embassy_net::{IpEndpoint, Stack, dns::DnsQueryType, tcp::TcpSocket};
use embassy_time::{Duration, Instant, Timer, with_timeout};
use embedded_io_async::{Read, Write};
use crate::led::LedPreset;
use crate::core::audio::AudioKind;
use crate::core::heart::heart_sent_counter_next_pure;
use crate::core::mqtt::{mqtt_own_topic_tx_should_apply, MQTT_PUBLISH_ACK_WAIT_MS};
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
    build_connect, build_puback, build_publish_qos1_retain, build_subscribe, consume_packet,
    itoa_into, ParsedMqtt,
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

fn next_packet_id(id: &mut u16) -> u16 {
    *id = id.wrapping_add(1);
    if *id == 0 {
        *id = 1;
    }
    *id
}

async fn mqtt_session_body<S: Read + Write>(
    stream: &mut S,
    pub_topic: &str,
    sub_topic: &str,
    own_topic: &str,
    lwt_topic: &str,
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
    tx_abs: &embassy_sync::mutex::Mutex<
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
    led_preset: &embassy_sync::signal::Signal<
        embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex,
        LedPreset,
    >,
) -> bool {
    let pkt = build_connect(client_id, user, pass, lwt_topic);
    if stream.write_all(&pkt).await.is_err() {
        return false;
    }
    let _ = stream.flush().await;
    let mut ack = [0u8; 4];
    if !read_exact(stream, &mut ack).await || ack[0] != 0x20 || ack[3] != 0x00 {
        println!("MQTT: CONNACK fail {ack:?}");
        return false;
    }

    let mut packet_id_seq = 0u16;
    let subscribe_partner = !sub_topic.is_empty();
    let subscribe_own =
        !own_topic.is_empty() && (!subscribe_partner || own_topic != sub_topic);
    if subscribe_partner {
        let sub_pkt = build_subscribe(next_packet_id(&mut packet_id_seq), sub_topic);
        if stream.write_all(&sub_pkt).await.is_err() {
            return false;
        }
    }
    if subscribe_own {
        let sub_pkt = build_subscribe(next_packet_id(&mut packet_id_seq), own_topic);
        if stream.write_all(&sub_pkt).await.is_err() {
            return false;
        }
    }
    if subscribe_partner || subscribe_own {
        let _ = stream.flush().await;
    }

    // Retained QoS1 `online` on LWT topic (parity with C++ CONNACK path).
    if !lwt_topic.is_empty() {
        let online_id = next_packet_id(&mut packet_id_seq);
        let online_pkt = build_publish_qos1_retain(lwt_topic, "online", online_id);
        if stream.write_all(&online_pkt).await.is_err() {
            return false;
        }
        let _ = stream.flush().await;
    }

    println!("MQTT: session up");
    mqtt_up.signal(());

    let mut acc = [0u8; 768];
    let mut acc_len = 0usize;
    let mut ping_at = Instant::now() + Duration::from_secs(45);
    let mut publish_in_flight = false;
    let mut pending_expected = 0i32;
    let mut pending_packet_id: Option<u16> = None;
    let mut puback_deadline: Option<Instant> = None;
    /// User bump (button/API): LED TxAck/TxFail on PUBACK outcome. Seed republish: no LED.
    let mut pending_led = false;
    let own_topic_bytes = own_topic.as_bytes();
    let sub_topic_bytes = sub_topic.as_bytes();

    loop {
        if publish_in_flight {
            if let Some(deadline) = puback_deadline {
                if Instant::now() >= deadline {
                    println!(
                        "MQTT: PUBACK timeout (wait_ms={})",
                        MQTT_PUBLISH_ACK_WAIT_MS
                    );
                    if pending_led {
                        led_preset.signal(LedPreset::TxFail);
                    }
                    publish_in_flight = false;
                    pending_packet_id = None;
                    puback_deadline = None;
                    pending_led = false;
                }
            }
        }

        if !publish_in_flight {
            if let Some(req) = bump.try_take() {
                // req <= 0: republish current abs (connect seed). req > 0: request TX+1.
                let user_bump = req > 0;
                let cur = *tx_abs.lock().await;
                let next = if !user_bump {
                    cur
                } else {
                    let n = heart_sent_counter_next_pure(cur);
                    if n == cur {
                        println!("MQTT: bump skipped (tx at max)");
                        led_preset.signal(LedPreset::TxFail);
                        continue;
                    }
                    n
                };
                let mut payload = heapless::String::<16>::new();
                itoa_into(&mut payload, next);
                let pid = next_packet_id(&mut packet_id_seq);
                let pub_pkt =
                    build_publish_qos1_retain(pub_topic, payload.as_str(), pid);
                if stream.write_all(&pub_pkt).await.is_err() {
                    if user_bump {
                        led_preset.signal(LedPreset::TxFail);
                    }
                    return true;
                }
                let _ = stream.flush().await;
                publish_in_flight = true;
                pending_expected = next;
                pending_packet_id = Some(pid);
                pending_led = user_bump;
                puback_deadline = Some(
                    Instant::now()
                        + Duration::from_millis(MQTT_PUBLISH_ACK_WAIT_MS as u64),
                );
                println!("MQTT: publish heart={next} (pid={pid})");
            }
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
                while let Some((used, parsed)) = consume_packet(&acc[..acc_len]) {
                    match parsed {
                        ParsedMqtt::Puback { packet_id } => {
                            if pending_packet_id == Some(packet_id) {
                                publish_in_flight = false;
                                pending_packet_id = None;
                                puback_deadline = None;
                                // Apply TX only after PUBACK (C++ heartSentCounterApplyAfterSuccessfulPublish).
                                {
                                    let mut t = tx_abs.lock().await;
                                    *t = pending_expected;
                                }
                                heart_dirty.signal(());
                                crate::sse_dirty::sse_mark(crate::core::constants::SSE_CHAYA);
                                if pending_led {
                                    led_preset.signal(LedPreset::TxAck);
                                }
                                pending_led = false;
                                println!("MQTT: PUBACK applied tx={pending_expected}");
                            }
                        }
                        ParsedMqtt::Publish {
                            topic,
                            payload_i32,
                            packet_id,
                            qos,
                            ..
                        } => {
                            if qos == 1 {
                                if let Some(pid) = packet_id {
                                    let ack = build_puback(pid);
                                    if stream.write_all(&ack).await.is_err() {
                                        return true;
                                    }
                                    let _ = stream.flush().await;
                                }
                            }
                            if let Some(v) = payload_i32 {
                                if !sub_topic_bytes.is_empty() && topic == sub_topic_bytes {
                                    let mut g = rx_abs.lock().await;
                                    if v > *g {
                                        *g = v;
                                        println!("MQTT: rx heart={v}");
                                        rx_beep.signal(AudioKind::Rx);
                                        heart_dirty.signal(());
                                        crate::sse_dirty::sse_mark(crate::core::constants::SSE_CHAYA);
                                    }
                                } else if !own_topic_bytes.is_empty()
                                    && topic == own_topic_bytes
                                {
                                    let mut local = tx_abs.lock().await;
                                    if mqtt_own_topic_tx_should_apply(
                                        v,
                                        *local,
                                        publish_in_flight,
                                        pending_expected,
                                    ) {
                                        *local = v;
                                        println!("MQTT: own-topic heart={v}");
                                        heart_dirty.signal(());
                                        crate::sse_dirty::sse_mark(crate::core::constants::SSE_CHAYA);
                                    }
                                }
                            }
                        }
                        ParsedMqtt::Pingresp
                        | ParsedMqtt::Connack { .. }
                        | ParsedMqtt::Other { .. } => {}
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
    own_topic: &'static str,
    lwt_topic: &'static str,
    _device_id: &'static str,
    bump: &'static embassy_sync::signal::Signal<
        embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex,
        i32,
    >,
    rx_abs: &'static embassy_sync::mutex::Mutex<
        embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex,
        i32,
    >,
    tx_abs: &'static embassy_sync::mutex::Mutex<
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
    led_preset: &'static embassy_sync::signal::Signal<
        embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex,
        LedPreset,
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
        crate::sse_dirty::sse_mark(
            crate::core::constants::SSE_CHAYA | crate::core::constants::SSE_MQTT,
        );
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
            let Ok(mut tls) =
                crate::tls::connect(socket, host, &mut rng, &mut tls_rx, &mut tls_tx).await
            else {
                println!("MQTT: TLS handshake failed");
                Timer::after(Duration::from_secs(3)).await;
                continue;
            };
            {
                let mut ok = mqtt_ok.lock().await;
                *ok = true;
            }
            crate::sse_dirty::sse_mark(
                crate::core::constants::SSE_CHAYA | crate::core::constants::SSE_MQTT,
            );
            mqtt_session_body(
                &mut tls,
                pub_topic,
                sub_topic,
                own_topic,
                lwt_topic,
                client_id,
                user,
                pass,
                bump,
                rx_abs,
                tx_abs,
                rx_beep,
                heart_dirty,
                mqtt_up,
                led_preset,
            )
            .await
        } else {
            {
                let mut ok = mqtt_ok.lock().await;
                *ok = true;
            }
            crate::sse_dirty::sse_mark(
                crate::core::constants::SSE_CHAYA | crate::core::constants::SSE_MQTT,
            );
            mqtt_session_body(
                &mut socket,
                pub_topic,
                sub_topic,
                own_topic,
                lwt_topic,
                client_id,
                user,
                pass,
                bump,
                rx_abs,
                tx_abs,
                rx_beep,
                heart_dirty,
                mqtt_up,
                led_preset,
            )
            .await
        };
        let _ = up;
        println!("MQTT: session down");
        Timer::after(Duration::from_secs(2)).await;
    }
}
