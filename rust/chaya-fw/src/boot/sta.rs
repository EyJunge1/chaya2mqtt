//! STA bring-up + MQTT/OTA/mDNS.

use chaya_fw::input::LedPreset;
use chaya_fw::mqtt::session::EspRng;
use chaya_fw::mqtt::MqttConfig;
use chaya_fw::ota::client::EspRng as OtaRng;
use chaya_fw::storage::config::{DEVICE_HOSTNAME, STA_HOSTNAME_PREFIX};
use chaya_fw::storage::flash_shared::FlashCell;
use chaya_fw::wifi::ops::WifiMutex;
use chaya_fw::wifi::WlanConfig;
use chaya_fw::BootReport;
use embassy_executor::Spawner;
use embassy_net::StackResources;
use embassy_sync::mutex::Mutex;
use esp_hal::rng::Rng;
use esp_println::println;
use esp_radio::wifi::{
    AuthenticationMethodConfig, Config, ControllerConfig, Interface, WifiController,
    sta::StationConfig,
};
use static_cell::StaticCell;

use crate::runtime::state::KvMutex;

macro_rules! mk_static {
    ($t:ty, $val:expr) => {{
        static STATIC_CELL: StaticCell<$t> = StaticCell::new();
        let x = STATIC_CELL.uninit().write(($val));
        x
    }};
}

pub async fn run_sta(
    spawner: Spawner,
    wifi: esp_hal::peripherals::WIFI<'static>,
    seed: u64,
    kv: &'static KvMutex,
    fw: &mut chaya_fw::Firmware,
    report: &BootReport,
    flash: &'static FlashCell,
) -> ! {
    let wlan = {
        let mut kv = kv.lock().await;
        WlanConfig::load(&mut *kv)
    };
    println!("STA → {}", wlan.ssid.as_str());

    let ssid = esp_radio::wifi::Ssid::try_from(wlan.ssid.as_str()).expect("ssid");
    let pass = wlan
        .pass
        .as_str()
        .try_into()
        .unwrap_or_else(|_| "".try_into().expect("empty pass"));
    let station_config = Config::Station(
        StationConfig::default()
            .with_ssid(ssid.clone())
            .with_authentication(AuthenticationMethodConfig::Wpa2Personal(pass)),
    );

    let device = Interface::station();
    let controller = WifiController::new(
        wifi,
        ControllerConfig::default().with_initial_config(station_config),
    )
    .expect("wifi");
    let wifi_ctrl: &'static WifiMutex = mk_static!(WifiMutex, Mutex::new(controller));

    {
        let mut s = crate::runtime::state::STA_SSID.lock().await;
        s.clear();
        let _ = s.push_str(wlan.ssid.as_str());
    }

    let net_config = embassy_net::Config::dhcpv4(Default::default());
    let (stack, runner) = embassy_net::new(
        device,
        net_config,
        mk_static!(StackResources<16>, StackResources::<16>::new()),
        seed,
    );

    spawner.spawn(crate::runtime::wifi::connection_sta(wifi_ctrl).unwrap());
    spawner.spawn(crate::runtime::wifi::wifi_scan_only_task(wifi_ctrl).unwrap());
    spawner.spawn(crate::runtime::net::net_task(runner).unwrap());

    println!("STA: waiting for DHCP…");
    stack.wait_config_up().await;
    fw.flags.wifi_ok = true;
    println!("STA: IP up");
    crate::runtime::state::LED_PRESET.signal(LedPreset::WifiUp);
    spawner.spawn(crate::runtime::ntp::ntp_task(stack).unwrap());

    let mqtt_cfg = {
        let mut kv = kv.lock().await;
        MqttConfig::load(&mut *kv)
    };
    if mqtt_cfg.broker_configured() {
        let host = crate::boot::util::leak(mqtt_cfg.host.as_str());
        let user = crate::boot::util::leak(mqtt_cfg.user.as_str());
        let pass = crate::boot::util::leak(mqtt_cfg.pass.as_str());
        let client_id = crate::boot::util::leak(report.device_id.as_str());
        let pub_topic = crate::boot::util::leak(mqtt_cfg.pub_topic(report.device_id.as_str()).as_str());
        let sub_topic = crate::boot::util::leak(mqtt_cfg.sub_topic().as_str());
        let port = mqtt_cfg.port;
        // seed outbound with current absolute
        crate::runtime::state::HEART_BUMP.signal(fw.heart.tx);
        let mqtt_rng = EspRng(Rng::new());
        spawner
            .spawn(
                crate::runtime::mqtt::mqtt_session_task(
                    stack, host, port, client_id, user, pass, pub_topic, sub_topic, mqtt_rng,
                )
                .unwrap(),
            );
    }

    let ota_rng = OtaRng(Rng::new());
    spawner.spawn(crate::runtime::ota::ota_task(stack, ota_rng, flash).unwrap());

    let hostname = {
        let mut h = heapless::String::<32>::new();
        let _ = h.push_str(STA_HOSTNAME_PREFIX);
        let _ = h.push_str(report.device_id.as_str());
        crate::boot::util::leak(h.as_str())
    };
    let _ = DEVICE_HOSTNAME;
    spawner.spawn(crate::runtime::net::mdns_task(stack, hostname).unwrap());

    crate::boot::http_start::start_http(stack, kv, report, false, None).await
}

