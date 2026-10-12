//! Mark OTA image valid after a stable STA window (cancel rollback).

use crate::async_rt::state::*;
use chaya_fw::config::flash_shared::{FlashCell, FlashProxy};
use chaya_fw::core::ota::{ota_health_window_elapsed, OTA_HEALTH_STABLE_MS};
use embassy_time::{Duration, Instant, Timer};
use esp_hal_ota::Ota;
use esp_println::println;

#[embassy_executor::task]
pub async fn ota_health_task(flash: &'static FlashCell) {
    let boot = Instant::now();
    let mut settled_at_ms: u32 = 0;
    let mut marked = false;
    loop {
        Timer::after(Duration::from_secs(2)).await;
        if marked {
            continue;
        }
        let mqtt_ok = *MQTT_OK.lock().await;
        let sta_ok = mqtt_ok || {
            let ssid = STA_SSID.lock().await;
            !ssid.is_empty()
        };
        let now_ms = boot.elapsed().as_millis() as u32;
        if sta_ok && settled_at_ms == 0 {
            settled_at_ms = if now_ms == 0 { 1 } else { now_ms };
        }
        if !sta_ok {
            settled_at_ms = 0;
            continue;
        }
        if !ota_health_window_elapsed(
            true,
            true,
            true,
            settled_at_ms,
            now_ms,
            OTA_HEALTH_STABLE_MS,
        ) {
            continue;
        }
        let mut ota = match Ota::new(FlashProxy(flash)) {
            Ok(o) => o,
            Err(_) => {
                println!("OTA: health init failed");
                Timer::after(Duration::from_secs(10)).await;
                continue;
            }
        };
        match ota.ota_mark_app_valid() {
            Ok(()) => {
                println!("OTA: marked app valid (rollback cancelled)");
                marked = true;
            }
            Err(_) => {
                println!("OTA: mark valid failed — will retry");
                Timer::after(Duration::from_secs(10)).await;
            }
        }
    }
}
