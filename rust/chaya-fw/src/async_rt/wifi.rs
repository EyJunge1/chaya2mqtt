//! WiFi Embassy tasks.

use chaya_fw::wifi::ops::{self as wifi_ops, WifiMutex};
use chaya_pure::wifi_recovery::{
    wlan_recovery_decide, WlanRecoveryAction, WlanRecoveryState, WLAN_RECOVERY_MAX_RESTARTS_PER_DAY,
};
use embassy_time::{Duration, Instant, Timer};
use esp_println::println;

use crate::async_rt::state::*;

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
    let boot = Instant::now();
    let mut recovery = WlanRecoveryState::default();
    let mut restarts_today: u8 = 0;
    loop {
        println!("STA: connecting…");
        let result = {
            let mut controller = wifi.lock().await;
            controller.connect_async().await
        };
        match result {
            Ok(info) => {
                println!("STA: connected ssid={}", info.ssid.as_str());
                recovery.link_down_since_ms = 0;
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
            }
        }

        // Recovery while link is down (between connect attempts / after disconnect).
        let now_ms = Instant::now().as_millis() as u32;
        let uptime_ms = boot.elapsed().as_millis() as u32;
        let ota_busy = {
            let st = OTA_STATUS.lock().await;
            st.installing || st.checking
        };
        let action = wlan_recovery_decide(
            false,
            false,
            ota_busy,
            true,
            now_ms,
            uptime_ms,
            &mut recovery,
            restarts_today,
            WLAN_RECOVERY_MAX_RESTARTS_PER_DAY,
        );
        match action {
            WlanRecoveryAction::ForcedReassoc => {
                println!("STA: recovery forced reassoc");
                WIFI_FORCE_REASSOC.signal(());
                let mut controller = wifi.lock().await;
                let _ = controller.disconnect_async().await;
                Timer::after(Duration::from_secs(2)).await;
            }
            WlanRecoveryAction::Restart => {
                println!("STA: recovery controlled restart");
                restarts_today = restarts_today.saturating_add(1);
                REBOOT.signal(());
                Timer::after(Duration::from_secs(5)).await;
            }
            WlanRecoveryAction::None => {
                Timer::after(Duration::from_secs(3)).await;
            }
        }
    }
}
