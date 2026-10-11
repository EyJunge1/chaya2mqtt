//! Device firmware: FlashKv + EPD + SoftAP/STA + captive DNS + HTTP/SSE + MQTT + GPIO.
//! Build: `cargo +esp build -p chaya-fw --features device --release`

#![no_std]
#![no_main]

extern crate alloc;

use alloc::boxed::Box;
use alloc::string::String;
use core::{net::Ipv4Addr, str::FromStr};

use chaya_fw::board::{battery_latch_on, display_power_enable, pins, sd_hold_off};
use chaya_fw::config::SETUP_AP_IP;
use chaya_fw::device::audio_hw::AudioHw;
use chaya_fw::device::battery_adc::BatteryAdc;
use chaya_fw::device::epd::{self, PaintMode};
use chaya_fw::device::flash_kv::FlashKv;
use chaya_fw::device::http;
use chaya_fw::device::mqtt_tcp::{self, EspRng};
use chaya_fw::device::ota_client::{self, EspRng as OtaRng, OtaStatus};
use chaya_fw::device::spa::{ADMIN_HTML, OPENAPI_JSON};
use chaya_fw::mqtt::MqttConfig;
use chaya_fw::platform::{ClockMs, GpioIn, GpioOut};
use chaya_fw::wifi::{WlanConfig, WlanService};
use chaya_fw::{boot, BootReport};
use embassy_executor::Spawner;
use embassy_net::{
    IpListenEndpoint, Ipv4Cidr, Runner, Stack, StackResources, StaticConfigV4, tcp::TcpSocket,
};
use embassy_sync::{
    blocking_mutex::raw::CriticalSectionRawMutex,
    mutex::Mutex,
    signal::Signal,
};
use embassy_time::{Duration, Timer};
use embedded_io_async::Write;
use esp_alloc as _;
use esp_backtrace as _;
use esp_hal::{
    clock::CpuClock,
    gpio::{Input, InputConfig, Level, Output, OutputConfig, Pull},
    ram,
    rng::Rng,
    timer::timg::TimerGroup,
};
use esp_println::println;
use esp_radio::wifi::{
    AuthenticationMethodConfig, Config, ControllerConfig, Interface, WifiController,
    ap::AccessPointConfig, sta::StationConfig,
};
use esp_storage::FlashStorage;
use static_cell::StaticCell;

esp_bootloader_esp_idf::esp_app_desc!();

macro_rules! mk_static {
    ($t:ty, $val:expr) => {{
        static STATIC_CELL: StaticCell<$t> = StaticCell::new();
        #[deny(unused_attributes)]
        let x = STATIC_CELL.uninit().write(($val));
        x
    }};
}

type Cs = CriticalSectionRawMutex;

static HEART_BUMP: Signal<Cs, i32> = Signal::new();
static AUDIO_BEEP: Signal<Cs, ()> = Signal::new();
static OTA_CHECK: Signal<Cs, ()> = Signal::new();
static HEART_RX: Mutex<Cs, i32> = Mutex::new(0);
static MQTT_OK: Mutex<Cs, bool> = Mutex::new(false);
static BAT_PCT: Mutex<Cs, i32> = Mutex::new(-1);
static BAT_MV: Mutex<Cs, i32> = Mutex::new(0);
static HEART_TX: Mutex<Cs, i32> = Mutex::new(0);
static OTA_STATUS: Mutex<Cs, OtaStatus> = Mutex::new(OtaStatus {
    last_tag: heapless::String::new(),
    last_error: heapless::String::new(),
    checking: false,
    flash_ready: false,
});

struct EarlyGpio {
    bat: Output<'static>,
    sd_clk: Output<'static>,
    sd_miso: Output<'static>,
    sd_mosi: Output<'static>,
    epd_en: Output<'static>,
}

impl GpioOut for EarlyGpio {
    fn set_output(&mut self, pin: u8, high: bool) {
        let level = if high { Level::High } else { Level::Low };
        match pin {
            p if p == pins::BAT_CONTROL => self.bat.set_level(level),
            p if p == pins::SD_CLK => self.sd_clk.set_level(level),
            p if p == pins::SD_MISO => self.sd_miso.set_level(level),
            p if p == pins::SD_MOSI => self.sd_mosi.set_level(level),
            p if p == pins::DISPLAY_PWR_EN => self.epd_en.set_level(level),
            _ => {}
        }
    }
}

struct MonoClock;
impl ClockMs for MonoClock {
    fn now_ms(&self) -> u32 {
        (embassy_time::Instant::now().as_millis() & 0xffff_ffff) as u32
    }
}

fn leak(s: &str) -> &'static str {
    Box::leak(String::from(s).into_boxed_str())
}

#[esp_hal::main]
async fn main(spawner: Spawner) -> ! {
    let config = esp_hal::Config::default().with_cpu_clock(CpuClock::max());
    let peripherals = esp_hal::init(config);

    esp_alloc::heap_allocator!(#[ram(reclaimed)] size: 64 * 1024);
    esp_alloc::heap_allocator!(size: 48 * 1024);

    let out_cfg = OutputConfig::default();
    let mut early = EarlyGpio {
        bat: Output::new(peripherals.GPIO17, Level::High, out_cfg),
        sd_clk: Output::new(peripherals.GPIO39, Level::Low, out_cfg),
        sd_miso: Output::new(peripherals.GPIO40, Level::Low, out_cfg),
        sd_mosi: Output::new(peripherals.GPIO41, Level::Low, out_cfg),
        epd_en: Output::new(peripherals.GPIO6, Level::High, out_cfg),
    };
    battery_latch_on(&mut early);
    sd_hold_off(&mut early);
    display_power_enable(&mut early, true);

    let timg0 = TimerGroup::new(peripherals.TIMG0);
    esp_rtos::start(timg0.timer0, peripherals.FROM_CPU_INTR0);

    let flash = FlashStorage::new(peripherals.FLASH);
    let mut kv = FlashKv::open(flash);
    let clock = MonoClock;
    let rng = Rng::new();
    let r0 = (rng.random() & 0xff) as u8;
    let r1 = (rng.random() & 0xff) as u8;
    let r2 = (rng.random() & 0xff) as u8;
    let (mut fw, report): (_, BootReport) = boot(&mut early, &mut kv, &clock, [r0, r1, r2]);
    println!("=== Chaya2MQTT {} === id={}", report.version, report.device_id);
    println!("ap_mode={} heart_ready={}", report.ap_mode, report.heart_ready);

    {
        let mut t = HEART_TX.lock().await;
        *t = fw.heart.tx;
        let mut r = HEART_RX.lock().await;
        *r = fw.heart.rx;
    }

    // LED + buttons (after early pins taken)
    let led = Output::new(peripherals.GPIO3, Level::High, out_cfg);
    let boot_btn = Input::new(
        peripherals.GPIO0,
        InputConfig::default().with_pull(Pull::Up),
    );
    let pwr_btn = Input::new(
        peripherals.GPIO18,
        InputConfig::default().with_pull(Pull::Up),
    );
    spawner.spawn(button_led_task(boot_btn, pwr_btn, led, early.bat).unwrap());

    // Battery ADC
    let bat = BatteryAdc::new(peripherals.ADC1, peripherals.GPIO4);
    spawner.spawn(battery_task(bat).unwrap());

    // Audio (ES8311 + PA)
    let audio = AudioHw::new(
        peripherals.I2C0,
        peripherals.GPIO47,
        peripherals.GPIO48,
        peripherals.GPIO42,
        peripherals.GPIO46,
        peripherals.GPIO14,
        peripherals.GPIO15,
        peripherals.GPIO38,
        peripherals.GPIO45,
        peripherals.I2S0,
    );
    spawner.spawn(audio_task(audio).unwrap());

    let paint_mode = if report.ap_mode {
        PaintMode::Splash
    } else {
        PaintMode::Heart
    };
    let wifi = peripherals.WIFI;
    let _ = epd::paint(
        peripherals.SPI2,
        peripherals.GPIO12,
        peripherals.GPIO13,
        peripherals.GPIO11,
        peripherals.GPIO10,
        peripherals.GPIO9,
        peripherals.GPIO8,
        paint_mode,
    );

    let seed = (rng.random() as u64) << 32 | rng.random() as u64;

    if report.ap_mode {
        run_softap(spawner, wifi, seed, &mut kv, &report).await;
    } else {
        run_sta(spawner, wifi, seed, &mut kv, &mut fw, &report).await;
    }
}

async fn run_softap(
    spawner: Spawner,
    wifi: esp_hal::peripherals::WIFI<'static>,
    seed: u64,
    kv: &mut FlashKv,
    report: &BootReport,
) -> ! {
    let access_point_config = Config::AccessPoint(
        AccessPointConfig::default().with_ssid(
            esp_radio::wifi::Ssid::try_from(WlanService::setup_ap_ssid()).expect("ssid"),
        ),
    );
    println!("SoftAP {} @ {}", WlanService::setup_ap_ssid(), SETUP_AP_IP);

    let device = Interface::access_point();
    let controller = WifiController::new(
        wifi,
        ControllerConfig::default().with_initial_config(access_point_config),
    )
    .expect("wifi");

    let gw_ip = Ipv4Addr::from_str(SETUP_AP_IP).unwrap();
    let net_config = embassy_net::Config::ipv4_static(StaticConfigV4 {
        address: Ipv4Cidr::new(gw_ip, 24),
        gateway: Some(gw_ip),
        dns_servers: Default::default(),
    });
    let (stack, runner) = embassy_net::new(
        device,
        net_config,
        mk_static!(StackResources<8>, StackResources::<8>::new()),
        seed,
    );

    spawner.spawn(connection_ap(controller).unwrap());
    spawner.spawn(net_task(runner).unwrap());
    spawner.spawn(run_dhcp(stack, SETUP_AP_IP).unwrap());
    spawner.spawn(run_captive_dns(stack, SETUP_AP_IP).unwrap());
    stack.wait_config_up().await;
    println!("HTTP+captive DNS http://{SETUP_AP_IP}/");

    http_loop(stack, kv, report, true).await
}

async fn run_sta(
    spawner: Spawner,
    wifi: esp_hal::peripherals::WIFI<'static>,
    seed: u64,
    kv: &mut FlashKv,
    fw: &mut chaya_fw::Firmware,
    report: &BootReport,
) -> ! {
    let wlan = WlanConfig::load(kv);
    println!("STA → {}", wlan.ssid.as_str());

    let ssid = esp_radio::wifi::Ssid::try_from(wlan.ssid.as_str()).expect("ssid");
    let pass = wlan
        .pass
        .as_str()
        .try_into()
        .unwrap_or_else(|_| "".try_into().expect("empty pass"));
    let station_config = Config::Station(
        StationConfig::default()
            .with_ssid(ssid)
            .with_authentication(AuthenticationMethodConfig::Wpa2Personal(pass)),
    );

    let device = Interface::station();
    let controller = WifiController::new(
        wifi,
        ControllerConfig::default().with_initial_config(station_config),
    )
    .expect("wifi");

    let net_config = embassy_net::Config::dhcpv4(Default::default());
    let (stack, runner) = embassy_net::new(
        device,
        net_config,
        mk_static!(StackResources<12>, StackResources::<12>::new()),
        seed,
    );

    spawner.spawn(connection_sta(controller).unwrap());
    spawner.spawn(net_task(runner).unwrap());

    println!("STA: waiting for DHCP…");
    stack.wait_config_up().await;
    fw.flags.wifi_ok = true;
    println!("STA: IP up");

    let mqtt_cfg = MqttConfig::load(kv);
    if mqtt_cfg.broker_configured() {
        let host = leak(mqtt_cfg.host.as_str());
        let user = leak(mqtt_cfg.user.as_str());
        let pass = leak(mqtt_cfg.pass.as_str());
        let client_id = leak(report.device_id.as_str());
        let pub_topic = leak(mqtt_cfg.pub_topic(report.device_id.as_str()).as_str());
        let sub_topic = leak(mqtt_cfg.sub_topic().as_str());
        let port = mqtt_cfg.port;
        // seed outbound with current absolute
        HEART_BUMP.signal(fw.heart.tx);
        let mqtt_rng = EspRng(Rng::new());
        spawner
            .spawn(
                mqtt_session_task(
                    stack, host, port, client_id, user, pass, pub_topic, sub_topic, mqtt_rng,
                )
                .unwrap(),
            );
        let ota_rng = OtaRng(Rng::new());
        spawner.spawn(ota_task(stack, ota_rng).unwrap());
    }

    http_loop(stack, kv, report, false).await
}

async fn http_loop(
    stack: Stack<'static>,
    kv: &mut FlashKv,
    report: &BootReport,
    ap_mode: bool,
) -> ! {
    let mut rx_buffer = [0; 2048];
    let mut tx_buffer = [0; 2048];
    let mut socket = TcpSocket::new(stack, &mut rx_buffer, &mut tx_buffer);
    socket.set_timeout(Some(Duration::from_secs(10)));
    let id = report.device_id.as_str();

    loop {
        if socket
            .accept(IpListenEndpoint {
                addr: None,
                port: 80,
            })
            .await
            .is_err()
        {
            continue;
        }

        let mut buffer = [0u8; 2048];
        let mut pos = 0usize;
        loop {
            match socket.read(&mut buffer[pos..]).await {
                Ok(0) => break,
                Ok(len) => {
                    pos += len;
                    if buffer[..pos].windows(4).any(|w| w == b"\r\n\r\n") {
                        if http::is_post(&buffer[..pos]) {
                            Timer::after(Duration::from_millis(30)).await;
                            if let Ok(n) = socket.read(&mut buffer[pos..]).await {
                                pos += n;
                            }
                        }
                        break;
                    }
                    if pos >= buffer.len() {
                        break;
                    }
                }
                Err(_) => break,
            }
        }

        let req = &buffer[..pos];
        let path = http::request_path(req);
        let wifi_ok = !ap_mode;
        let mqtt_ok = *MQTT_OK.lock().await;
        let heart_tx = *HEART_TX.lock().await;
        let heart_rx = *HEART_RX.lock().await;
        let bat_pct = *BAT_PCT.lock().await;
        let bat_mv = *BAT_MV.lock().await;

        let resp = if ap_mode && http::is_captive_probe(path) && http::is_get(req) {
            http::response_redirect(http::captive_landing())
        } else if ap_mode && path == "/success.txt" && http::is_get(req) {
            http::response_empty(200)
        } else if ap_mode && path == "/wpad.dat" && http::is_get(req) {
            http::response_empty(404)
        } else if path == "/api/status" && http::is_get(req) {
            let body = http::json_status(
                id, ap_mode, wifi_ok, mqtt_ok, heart_tx, heart_rx, bat_pct, bat_mv,
            );
            http::response_ok("application/json", body.as_str())
        } else if path == "/api/events" && http::is_get(req) {
            http::sse_event(heart_tx, heart_rx, mqtt_ok, bat_pct)
        } else if path == "/api/wifi" && http::is_post(req) && ap_mode {
            let b = http::body(req);
            let mut cfg = WlanConfig::default();
            if let Some(ssid) = http::form_value(b, "ssid") {
                let _ = cfg.ssid.push_str(ssid);
            }
            if let Some(pass) = http::form_value(b, "pass") {
                let _ = cfg.pass.push_str(pass);
            }
            if cfg.has_sta() && cfg.save(kv) {
                println!("WiFi saved — power-cycle for STA");
            }
            http::response_redirect("/")
        } else if path == "/api/mqtt" && http::is_post(req) && ap_mode {
            let b = http::body(req);
            let mut cfg = MqttConfig::load(kv);
            if let Some(h) = http::form_value(b, "host") {
                cfg.host.clear();
                let _ = cfg.host.push_str(h);
            }
            if let Some(p) = http::form_value(b, "port") {
                if let Ok(v) = p.parse::<u16>() {
                    cfg.port = v;
                }
            }
            if let Some(u) = http::form_value(b, "user") {
                cfg.user.clear();
                let _ = cfg.user.push_str(u);
            }
            if let Some(p) = http::form_value(b, "pass") {
                cfg.pass.clear();
                let _ = cfg.pass.push_str(p);
            }
            if let Some(p) = http::form_value(b, "partner") {
                cfg.partner_id.clear();
                let _ = cfg.partner_id.push_str(p);
            }
            let _ = cfg.save(kv);
            println!("MQTT cfg saved");
            http::response_redirect("/")
        } else if path == "/api/send" && http::is_post(req) && !ap_mode {
            let next = {
                let mut t = HEART_TX.lock().await;
                *t = t.saturating_add(1);
                *t
            };
            HEART_BUMP.signal(next);
            AUDIO_BEEP.signal(());
            let mut heart = chaya_fw::heart::HeartCounters::load(kv);
            heart.tx = next;
            let _ = heart.save(kv);
            http::response_redirect("/")
        } else if path == "/api/ota/check" && http::is_post(req) && !ap_mode {
            OTA_CHECK.signal(());
            http::response_redirect("/")
        } else if path == "/api/ota/status" && http::is_get(req) {
            let st = OTA_STATUS.lock().await;
            let mut body = heapless::String::<192>::new();
            let _ = body.push_str("{\"tag\":\"");
            let _ = body.push_str(st.last_tag.as_str());
            let _ = body.push_str("\",\"checking\":");
            let _ = body.push_str(if st.checking { "true" } else { "false" });
            let _ = body.push_str(",\"err\":\"");
            let _ = body.push_str(st.last_error.as_str());
            let _ = body.push_str("\"}");
            http::response_ok("application/json", body.as_str())
        } else if path == "/openapi.json" && http::is_get(req) {
            http::response_ok("application/json", OPENAPI_JSON)
        } else if (path == "/" || path == "/index.html") && http::is_get(req) {
            // stream SPA (too large for heapless response helper)
            let _ = socket
                .write_all(b"HTTP/1.0 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n")
                .await;
            let _ = socket.write_all(ADMIN_HTML.as_bytes()).await;
            let _ = socket.flush().await;
            Timer::after(Duration::from_millis(80)).await;
            socket.close();
            Timer::after(Duration::from_millis(80)).await;
            socket.abort();
            continue;
        } else {
            let body = http::html_index(id, ap_mode, wifi_ok, mqtt_ok, heart_tx, heart_rx, bat_pct);
            http::response_ok("text/html", body.as_str())
        };

        let _ = socket.write_all(resp.as_bytes()).await;
        let _ = socket.flush().await;
        Timer::after(Duration::from_millis(80)).await;
        socket.close();
        Timer::after(Duration::from_millis(80)).await;
        socket.abort();
    }
}

#[embassy_executor::task]
async fn mqtt_session_task(
    stack: Stack<'static>,
    host: &'static str,
    port: u16,
    client_id: &'static str,
    user: &'static str,
    pass: &'static str,
    pub_topic: &'static str,
    sub_topic: &'static str,
    rng: EspRng,
) {
    mqtt_tcp::session_loop(
        stack, host, port, client_id, user, pass, pub_topic, sub_topic, &HEART_BUMP, &HEART_RX,
        &MQTT_OK, rng,
    )
    .await
}

#[embassy_executor::task]
async fn audio_task(mut audio: AudioHw) {
    let _ = audio.init_codec();
    loop {
        AUDIO_BEEP.wait().await;
        audio.beep().await;
    }
}

#[embassy_executor::task]
async fn ota_task(stack: Stack<'static>, mut rng: OtaRng) {
    loop {
        OTA_CHECK.wait().await;
        let mut st = OTA_STATUS.lock().await;
        st.checking = true;
        drop(st);
        let mut local = OtaStatus::default();
        let _ = ota_client::check_latest(stack, &mut rng, &mut local).await;
        let mut st = OTA_STATUS.lock().await;
        *st = local;
    }
}

#[embassy_executor::task]
async fn battery_task(mut bat: BatteryAdc) {
    loop {
        bat.poll();
        {
            let mut p = BAT_PCT.lock().await;
            *p = bat.last_pct;
            let mut m = BAT_MV.lock().await;
            *m = bat.last_mv;
        }
        Timer::after(Duration::from_secs(30)).await;
    }
}

struct BtnGpio {
    boot: Input<'static>,
    pwr: Input<'static>,
}

impl GpioIn for BtnGpio {
    fn get_input(&mut self, pin: u8) -> bool {
        match pin {
            p if p == pins::BUTTON => self.boot.is_high(),
            p if p == pins::PWR_BUTTON => self.pwr.is_high(),
            _ => true,
        }
    }
}

struct LedGpio {
    led: Output<'static>,
}

impl GpioOut for LedGpio {
    fn set_output(&mut self, pin: u8, high: bool) {
        if pin == pins::BUTTON_LED {
            self.led.set_level(if high { Level::High } else { Level::Low });
        }
    }
}

#[embassy_executor::task]
async fn button_led_task(
    boot: Input<'static>,
    pwr: Input<'static>,
    led: Output<'static>,
    mut bat_latch: Output<'static>,
) {
    let mut btns = BtnGpio { boot, pwr };
    let mut led_gpio = LedGpio { led };
    let mut button = chaya_fw::button::ButtonService::default();
    let mut led_svc = chaya_fw::led::LedService::default();
    led_svc.startup_blink();
    let clock = MonoClock;

    loop {
        let _ = led_svc.tick_phase_done(&mut led_gpio);
        if button.poll_boot_press(&mut btns, &clock) {
            let next = {
                let mut t = HEART_TX.lock().await;
                *t = t.saturating_add(1);
                *t
            };
            HEART_BUMP.signal(next);
            AUDIO_BEEP.signal(());
            led_svc.start_blink(1, 40, 40);
            println!("BTN: heart bump → {next}");
        }
        if button.soft_off_ready(&mut btns, &clock) {
            println!("PWR: soft-off");
            bat_latch.set_level(Level::Low);
        }
        Timer::after(Duration::from_millis(20)).await;
    }
}

#[embassy_executor::task]
async fn run_dhcp(stack: Stack<'static>, gw_ip_addr: &'static str) {
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
async fn run_captive_dns(stack: Stack<'static>, gw_ip_addr: &'static str) {
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

#[embassy_executor::task]
async fn connection_ap(controller: WifiController<'static>) {
    loop {
        let _ = controller.wait_for_access_point_connected_event_async().await;
        Timer::after(Duration::from_millis(1000)).await;
    }
}

#[embassy_executor::task]
async fn connection_sta(mut controller: WifiController<'static>) {
    loop {
        println!("STA: connecting…");
        match controller.connect_async().await {
            Ok(info) => {
                println!("STA: connected ssid={}", info.ssid.as_str());
                let _ = controller.wait_for_disconnect_async().await;
                println!("STA: disconnected");
            }
            Err(e) => {
                println!("STA: connect err {e:?}");
                Timer::after(Duration::from_secs(3)).await;
            }
        }
    }
}

#[embassy_executor::task]
async fn net_task(mut runner: Runner<'static, Interface>) {
    runner.run().await
}
