//! MQTT 3.1.1 session on `mqttrust`.
//!
//! The crate owns encode/decode, keepalive, reconnect and QoS1 PUBACK.
//! Heart increment, own-topic echo and LED stay here. TLS stays `crate::tls::connect`
//! (host-pin or mbedtls). `mqttrust::ConnectionError` is private, so the socket
//! is plugged in through `NalTransport` plus `embedded_nal_async::TcpConnect`.

use core::cell::UnsafeCell;
use core::net::{IpAddr, Ipv4Addr, SocketAddr};

use embassy_futures::select::{select, select3, Either};
use embassy_net::tcp::client::{TcpClient, TcpClientState, TcpConnection};
use embassy_net::tcp::Error as TcpError;
use embassy_net::{dns::DnsQueryType, IpAddress, Stack};
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::mutex::Mutex;
use embassy_sync::signal::Signal;
use embassy_time::{with_timeout, Duration, Timer};
use embedded_io_async::{ErrorType, Read, Write};
use embedded_nal_async::TcpConnect;
use esp_println::println;
use mqttrust::transport::embedded_nal::NalTransport;
use mqttrust::{
    Broker, Config, LastWill, MqttClient, Publish, QoS, State, Subscribe, SubscribeTopic,
};

use crate::core::audio::AudioKind;
use crate::core::constants::{SSE_CHAYA, SSE_MQTT};
use crate::core::heart::heart_sent_counter_next_pure;
use crate::core::mqtt::{
    mqtt_own_topic_tx_should_apply, parse_mqtt_payload_i32, MQTT_PUBLISH_ACK_WAIT_MS,
};
use crate::led::LedPreset;
use crate::sse_dirty::sse_mark;

pub use crate::util::EspRng;

type Cs = CriticalSectionRawMutex;
type Client<'a> = MqttClient<'a, Cs>;

const TCP_TX: usize = 1536;
const TCP_RX: usize = 2048;
const MQTT_TX: usize = 1024;
const MQTT_RX: usize = 1024;

struct Flight {
    active: bool,
    expected: i32,
}

enum MqttIo<'a> {
    Tcp(TcpConnection<'a, 1, TCP_TX, TCP_RX>),
    Tls(crate::tls::TlsStream<'a, TcpConnection<'a, 1, TCP_TX, TCP_RX>>),
}

impl ErrorType for MqttIo<'_> {
    type Error = TcpError;
}

impl Read for MqttIo<'_> {
    async fn read(&mut self, buf: &mut [u8]) -> Result<usize, Self::Error> {
        match self {
            Self::Tcp(sock) => sock.read(buf).await,
            Self::Tls(sock) => sock.read(buf).await.map_err(|_| TcpError::ConnectionReset),
        }
    }
}

impl Write for MqttIo<'_> {
    async fn write(&mut self, buf: &[u8]) -> Result<usize, Self::Error> {
        match self {
            Self::Tcp(sock) => sock.write(buf).await,
            Self::Tls(sock) => sock.write(buf).await.map_err(|_| TcpError::ConnectionReset),
        }
    }

    async fn flush(&mut self) -> Result<(), Self::Error> {
        match self {
            Self::Tcp(sock) => sock.flush().await,
            Self::Tls(sock) => sock.flush().await.map_err(|_| TcpError::ConnectionReset),
        }
    }
}

/// Embassy TCP pool plus optional TLS buffers. One connection at a time.
struct MqttNet<'d> {
    inner: TcpClient<'d, 1, TCP_TX, TCP_RX>,
    host: &'static str,
    port: u16,
    use_tls: bool,
    rng: UnsafeCell<EspRng>,
    tls_rx: UnsafeCell<[u8; 8192]>,
    tls_tx: UnsafeCell<[u8; 4096]>,
}

impl<'d> TcpConnect for MqttNet<'d> {
    type Error = TcpError;
    type Connection<'a>
        = MqttIo<'a>
    where
        Self: 'a;

    async fn connect<'a>(
        &'a self,
        remote: SocketAddr,
    ) -> Result<Self::Connection<'a>, Self::Error> {
        let host = self.host;
        let use_tls = self.use_tls;
        let rng_p = self.rng.get();
        let rx_p = self.tls_rx.get();
        let tx_p = self.tls_tx.get();
        println!(
            "MQTT: connect {host}:{} ({})",
            self.port,
            if use_tls { "TLS" } else { "TCP" }
        );
        let tcp = TcpConnect::connect(&self.inner, remote).await?;
        if !use_tls {
            return Ok(MqttIo::Tcp(tcp));
        }
        // SAFETY: `NalTransport` holds a single socket. `TcpConnection` only
        // touches the TCP pool in `inner`. The RNG and TLS buffers belong to
        // this stream until it is dropped, then the next `connect` may reuse them.
        let rng = unsafe { &mut *rng_p };
        let tls_rx = unsafe { &mut *rx_p };
        let tls_tx = unsafe { &mut *tx_p };
        match crate::tls::connect(tcp, host, rng, tls_rx, tls_tx).await {
            Ok(tls) => Ok(MqttIo::Tls(tls)),
            Err(()) => {
                println!("MQTT: TLS handshake failed");
                Err(TcpError::ConnectionReset)
            }
        }
    }
}

struct ResolvingBroker {
    stack: Stack<'static>,
    host: &'static str,
    port: u16,
}

impl Broker for ResolvingBroker {
    async fn get_address(&mut self) -> Option<SocketAddr> {
        match self.stack.dns_query(self.host, DnsQueryType::A).await {
            Ok(addrs) => {
                let Some(ip) = addrs.first().copied() else {
                    println!("MQTT: DNS empty for {}", self.host);
                    return None;
                };
                let IpAddress::Ipv4(v4) = ip else {
                    println!("MQTT: DNS not IPv4 for {}", self.host);
                    return None;
                };
                let oct = v4.octets();
                Some(SocketAddr::new(
                    IpAddr::V4(Ipv4Addr::new(oct[0], oct[1], oct[2], oct[3])),
                    self.port,
                ))
            }
            Err(e) => {
                println!("MQTT: DNS err {e:?}");
                None
            }
        }
    }

    fn get_hostname(&self) -> Option<&str> {
        Some(self.host)
    }
}

fn mqtt_reconnect_backoff(attempt: u8) -> Option<Duration> {
    let shift = u32::from(attempt.min(4));
    let secs = 2u64.saturating_mul(1u64 << shift);
    Some(Duration::from_secs(secs.min(30)))
}

fn heart_digits(n: i32) -> heapless::String<16> {
    let mut s = heapless::String::new();
    let mut buf = itoa::Buffer::new();
    let _ = s.push_str(buf.format(n));
    s
}

async fn set_mqtt_ok(mqtt_ok: &Mutex<Cs, bool>, up: bool) {
    let mut ok = mqtt_ok.lock().await;
    if *ok != up {
        *ok = up;
        sse_mark(SSE_CHAYA | SSE_MQTT);
    }
}

async fn publish_retained(client: &Client<'_>, topic: &str, payload: &[u8]) -> Result<(), ()> {
    let packet = Publish::builder()
        .topic_name(topic)
        .payload(payload)
        .qos(QoS::AtLeastOnce)
        .retain(true)
        .build();
    match with_timeout(
        Duration::from_millis(u64::from(MQTT_PUBLISH_ACK_WAIT_MS)),
        client.publish(packet),
    )
    .await
    {
        Ok(Ok(())) => Ok(()),
        Ok(Err(e)) => {
            println!("MQTT: publish err {e:?}");
            Err(())
        }
        Err(_) => {
            println!(
                "MQTT: PUBACK timeout (wait_ms={})",
                MQTT_PUBLISH_ACK_WAIT_MS
            );
            Err(())
        }
    }
}

async fn apply_incoming(
    topic: &str,
    payload: &[u8],
    sub_topic: &str,
    own_topic: &str,
    flight: &Mutex<Cs, Flight>,
    rx_abs: &Mutex<Cs, i32>,
    tx_abs: &Mutex<Cs, i32>,
    rx_beep: &Signal<Cs, AudioKind>,
    heart_dirty: &Signal<Cs, ()>,
) {
    let Some(v) = parse_mqtt_payload_i32(payload) else {
        return;
    };
    if !sub_topic.is_empty() && topic == sub_topic {
        let mut g = rx_abs.lock().await;
        if v > *g {
            *g = v;
            println!("MQTT: rx heart={v}");
            rx_beep.signal(AudioKind::Rx);
            heart_dirty.signal(());
            sse_mark(SSE_CHAYA);
        }
        return;
    }
    if own_topic.is_empty() || topic != own_topic {
        return;
    }
    let (active, expected) = {
        let g = flight.lock().await;
        (g.active, g.expected)
    };
    let mut local = tx_abs.lock().await;
    if mqtt_own_topic_tx_should_apply(v, *local, active, expected) {
        *local = v;
        println!("MQTT: own-topic heart={v}");
        heart_dirty.signal(());
        sse_mark(SSE_CHAYA);
    }
}

/// Long-lived session. TLS when `port >= 8883`.
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
    bump: &'static Signal<Cs, i32>,
    rx_abs: &'static Mutex<Cs, i32>,
    tx_abs: &'static Mutex<Cs, i32>,
    mqtt_ok: &'static Mutex<Cs, bool>,
    rx_beep: &'static Signal<Cs, AudioKind>,
    heart_dirty: &'static Signal<Cs, ()>,
    mqtt_up: &'static Signal<Cs, ()>,
    led_preset: &'static Signal<Cs, LedPreset>,
    rng: EspRng,
) -> ! {
    if client_id.len() > 16 {
        println!("MQTT: client id longer than 16");
    }
    let tcp_state = TcpClientState::<1, TCP_TX, TCP_RX>::new();
    let mut tcp = TcpClient::new(stack, &tcp_state);
    tcp.set_timeout(Some(Duration::from_secs(90)));
    let net = MqttNet {
        inner: tcp,
        host,
        port,
        use_tls: port >= 8883,
        rng: UnsafeCell::new(rng),
        tls_rx: UnsafeCell::new([0; 8192]),
        tls_tx: UnsafeCell::new([0; 4096]),
    };
    loop {
        set_mqtt_ok(mqtt_ok, false).await;
        let mut transport = NalTransport::new(&net, ResolvingBroker { stack, host, port });
        let mut mqtt_state = State::<Cs, MQTT_TX, MQTT_RX>::new();
        let will = LastWill::builder()
            .topic(lwt_topic)
            .data(b"offline".as_slice())
            .qos(QoS::AtLeastOnce)
            .retained(true)
            .build();
        let config = Config::builder()
            .client_id(client_id.try_into().unwrap_or_default())
            .keepalive_interval(Duration::from_secs(60))
            .backoff_algo(mqtt_reconnect_backoff)
            .maybe_username((!user.is_empty()).then_some(user))
            .maybe_password((!pass.is_empty()).then_some(pass.as_bytes()))
            .maybe_last_will((!lwt_topic.is_empty()).then_some(will))
            .build();
        let (mut mqtt_stack, client) = mqttrust::new(&mut mqtt_state, config);
        let flight = Mutex::new(Flight {
            active: false,
            expected: 0,
        });
        let subscribed = Signal::<Cs, ()>::new();

        let stack_fut = mqtt_stack.run(&mut transport);
        let rx_fut = async {
            client.wait_connected().await;
            let partner = !sub_topic.is_empty();
            let own = !own_topic.is_empty() && (!partner || own_topic != sub_topic);
            if !partner && !own {
                subscribed.signal(());
                core::future::pending::<()>().await;
                return;
            }
            let sub_t = SubscribeTopic::builder()
                .topic_path(sub_topic)
                .maximum_qos(QoS::AtLeastOnce)
                .build();
            let own_t = SubscribeTopic::builder()
                .topic_path(own_topic)
                .maximum_qos(QoS::AtLeastOnce)
                .build();
            let pair = [sub_t, own_t];
            let topics: &[SubscribeTopic<'_>] = match (partner, own) {
                (true, true) => &pair[..],
                (true, false) => &pair[..1],
                (false, true) => &pair[1..],
                (false, false) => &[],
            };
            let packet = Subscribe::builder().topics(topics).build();
            let mut subscription = match client.subscribe::<2>(packet).await {
                Ok(sub) => sub,
                Err(e) => {
                    println!("MQTT: subscribe failed {e:?}");
                    return;
                }
            };
            subscribed.signal(());
            loop {
                match subscription.next_message().await {
                    Some(msg) => {
                        apply_incoming(
                            msg.topic_name(),
                            msg.payload(),
                            sub_topic,
                            own_topic,
                            &flight,
                            rx_abs,
                            tx_abs,
                            rx_beep,
                            heart_dirty,
                        )
                        .await;
                    }
                    None => {
                        println!("MQTT: subscription dropped");
                        return;
                    }
                }
            }
        };
        let tx_fut = async {
            subscribed.wait().await;
            loop {
                client.wait_connected().await;
                if !lwt_topic.is_empty() {
                    match publish_retained(&client, lwt_topic, b"online").await {
                        Ok(()) => {}
                        Err(()) => {
                            println!("MQTT: online publish failed");
                            Timer::after(Duration::from_secs(2)).await;
                            continue;
                        }
                    }
                }
                println!("MQTT: session up");
                mqtt_up.signal(());
                set_mqtt_ok(mqtt_ok, true).await;
                loop {
                    if !client.is_connected().await {
                        break;
                    }
                    match select(bump.wait(), async {
                        while client.wait_connection_change().await {}
                    })
                    .await
                    {
                        Either::Second(()) => break,
                        Either::First(req) => {
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
                            {
                                let mut g = flight.lock().await;
                                g.active = true;
                                g.expected = next;
                            }
                            let payload = heart_digits(next);
                            let published =
                                publish_retained(&client, pub_topic, payload.as_bytes()).await;
                            if published.is_ok() {
                                *tx_abs.lock().await = next;
                                heart_dirty.signal(());
                                sse_mark(SSE_CHAYA);
                                if user_bump {
                                    led_preset.signal(LedPreset::TxAck);
                                }
                                println!("MQTT: PUBACK applied tx={next}");
                            } else if user_bump {
                                led_preset.signal(LedPreset::TxFail);
                            }
                            let mut g = flight.lock().await;
                            g.active = false;
                        }
                    }
                }
                set_mqtt_ok(mqtt_ok, false).await;
                println!("MQTT: session down");
            }
        };

        select3(stack_fut, rx_fut, tx_fut).await;
        println!("MQTT: session down");
        Timer::after(Duration::from_secs(2)).await;
    }
}
