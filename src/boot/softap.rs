//! SoftAP (APSTA) bring-up.

use core::net::Ipv4Addr;
use core::str::FromStr;

use chaya_fw::config::{DEVICE_HOSTNAME, SETUP_AP_IP};
use chaya_fw::wifi::ops::WifiMutex;
use chaya_fw::wifi::WlanService;
use chaya_fw::BootReport;
use embassy_executor::Spawner;
use embassy_net::{Ipv4Cidr, StackResources, StaticConfigV4};
use embassy_sync::mutex::Mutex;
use esp_println::println;
use esp_radio::wifi::{
    AuthenticationMethodConfig, Config, ControllerConfig, Interface, WifiController,
    ap::AccessPointConfig, sta::StationConfig,
};
use static_cell::StaticCell;

use crate::async_rt::state::KvMutex;

macro_rules! mk_static {
    ($t:ty, $val:expr) => {{
        static STATIC_CELL: StaticCell<$t> = StaticCell::new();
        let x = STATIC_CELL.uninit().write(($val));
        x
    }};
}

pub async fn run_softap(
    spawner: Spawner,
    wifi: esp_hal::peripherals::WIFI<'static>,
    seed: u64,
    kv: &'static KvMutex,
    report: &BootReport,
    ap_psk: &'static str,
) -> ! {
    let pass = ap_psk
        .try_into()
        .unwrap_or_else(|_| "Chaya2MQTTSetupKey!".try_into().expect("fallback psk"));
    let ap_config = AccessPointConfig::default()
        .with_ssid(
            esp_radio::wifi::Ssid::try_from(WlanService::setup_ap_ssid()).expect("ssid"),
        )
        .with_authentication(AuthenticationMethodConfig::Wpa2Personal(pass));
    // APSTA so SoftAP setup can scan / connection-test without dropping the captive portal.
    let radio_config = Config::AccessPointStation(StationConfig::default(), ap_config);
    println!(
        "SoftAP {} @ {} (WPA2, APSTA)",
        WlanService::setup_ap_ssid(),
        SETUP_AP_IP
    );

    let _sta_iface = Interface::station();
    let device = Interface::access_point();
    let controller = WifiController::new(
        wifi,
        ControllerConfig::default().with_initial_config(radio_config),
    )
    .expect("wifi");
    let wifi_ctrl: &'static WifiMutex = mk_static!(WifiMutex, Mutex::new(controller));

    let gw_ip = Ipv4Addr::from_str(SETUP_AP_IP).unwrap();
    let net_config = embassy_net::Config::ipv4_static(StaticConfigV4 {
        address: Ipv4Cidr::new(gw_ip, 24),
        gateway: Some(gw_ip),
        dns_servers: Default::default(),
    });
    let (stack, runner) = embassy_net::new(
        device,
        net_config,
        mk_static!(StackResources<16>, StackResources::<16>::new()),
        seed,
    );

    {
        let mut st = crate::async_rt::state::WIFI_TEST_STATE.lock().await;
        st.clear();
        let _ = st.push_str("idle");
    }

    spawner.spawn(crate::async_rt::wifi::wifi_ops_task(wifi_ctrl, ap_psk).unwrap());
    spawner.spawn(crate::async_rt::net::net_task(runner).unwrap());
    spawner.spawn(crate::async_rt::dhcp::run_dhcp(stack, SETUP_AP_IP).unwrap());
    spawner.spawn(crate::async_rt::dhcp::run_captive_dns(stack, SETUP_AP_IP).unwrap());
    stack.wait_config_up().await;
    println!("HTTP+captive DNS http://{SETUP_AP_IP}/");

    crate::boot::http_start::start_http(stack, kv, report, true, Some(ap_psk), DEVICE_HOSTNAME)
        .await
}

