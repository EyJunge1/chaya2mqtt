//! E-Paper redraw task.
use chaya_fw::display::epd::{EpdPanel, Scene};
use embassy_futures::select::{select, Either};
use embassy_time::{Duration, Timer};
use esp_println::println;
use crate::async_rt::state::*;

#[embassy_executor::task]
pub async fn epd_task(
    mut panel: EpdPanel,
    ap_mode: bool,
    ssid: &'static str,
    ap_psk: &'static str,
) {
    if ap_mode {
        let mut s = heapless::String::<32>::new();
        let _ = s.push_str(ssid);
        let mut p = heapless::String::<32>::new();
        let _ = p.push_str(ap_psk);
        let _ = panel.show(&Scene::SoftAp { ssid: s, psk: p });
        loop {
            if EPD_POWER_OFF.try_take().is_some() {
                println!("EPD: power off (soft-off)");
                let _ = panel.power_off();
                loop {
                    Timer::after(Duration::from_secs(3600)).await;
                }
            }
            Timer::after(Duration::from_millis(200)).await;
        }
    }
    // initial heart frame
    {
        let tx = *HEART_TX.lock().await;
        let rx = *HEART_RX.lock().await;
        let bat = (*BAT_PCT.lock().await).max(0);
        let mqtt = *MQTT_OK.lock().await;
        let _ = panel.show(&Scene::Heart {
            tx,
            rx,
            bat_pct: bat,
            cracked: !mqtt,
        });
    }
    loop {
        match select(EPD_REDRAW.wait(), EPD_POWER_OFF.wait()).await {
            Either::First(()) => {
                Timer::after(Duration::from_secs(3)).await;
                while EPD_REDRAW.try_take().is_some() {}
                let tx = *HEART_TX.lock().await;
                let rx = *HEART_RX.lock().await;
                let bat = (*BAT_PCT.lock().await).max(0);
                let mqtt = *MQTT_OK.lock().await;
                let _ = panel.show(&Scene::Heart {
                    tx,
                    rx,
                    bat_pct: bat,
                    cracked: !mqtt,
                });
            }
            Either::Second(()) => {
                println!("EPD: power off (soft-off)");
                let _ = panel.power_off();
                loop {
                    Timer::after(Duration::from_secs(3600)).await;
                }
            }
        }
    }
}
