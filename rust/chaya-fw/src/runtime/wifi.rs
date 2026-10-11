//! WiFi Embassy tasks.

use chaya_fw::wifi::ops::{self as wifi_ops, WifiMutex};
use embassy_time::{Duration, Timer};
use esp_println::println;

use crate::runtime::state::*;

#[embassy_executor::task]
pub async fn wifi_ops_task(wifi: &'static WifiMutex, ap_psk: &'static str) {
    wifi_ops::run_wifi_ops(
        wifi,
        ap_psk,
        &WIFI_SCAN,
        &WIFI_PENDING,
        &WIFI_TEST_STATE,
        &WIFI_TEST_SSID,
        &WIFI_TEST_OWNS,
        &SCAN_KICK,
        &WIFI_TEST_KICK,
    )
    .await
}

#[embassy_executor::task]
pub async fn wifi_scan_only_task(wifi: &'static WifiMutex) {
    wifi_ops::run_scan_only(wifi, &WIFI_SCAN, &SCAN_KICK).await
}

#[embassy_executor::task]
pub async fn connection_sta(wifi: &'static WifiMutex) {
    loop {
        println!("STA: connecting…");
        let result = {
            let mut controller = wifi.lock().await;
            controller.connect_async().await
        };
        match result {
            Ok(info) => {
                println!("STA: connected ssid={}", info.ssid.as_str());
                if let Ok(rssi) = {
                    let ctrl = wifi.lock().await;
                    ctrl.rssi()
                } {
                    *STA_RSSI.lock().await = rssi;
                }
                {
                    let controller = wifi.lock().await;
                    let _ = controller.wait_for_disconnect_async().await;
                }
                println!("STA: disconnected");
            }
            Err(e) => {
                println!("STA: connect err {e:?}");
                Timer::after(Duration::from_secs(3)).await;
            }
        }
    }
}

