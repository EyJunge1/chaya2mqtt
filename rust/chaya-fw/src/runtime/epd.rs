//! E-Paper redraw task.
use chaya_fw::display::epd::{EpdPanel, Scene};
use embassy_time::{Duration, Timer};
use crate::runtime::state::*;

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
            Timer::after(Duration::from_secs(3600)).await;
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
        EPD_REDRAW.wait().await;
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
}
