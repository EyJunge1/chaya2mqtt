//! E-Paper redraw task.
use crate::async_rt::state::*;
use chaya_fw::core::display::{DisplayHeartIcon, DISPLAY_OFFLINE_GRACE_MS};
use chaya_fw::display::epd::{EpdPanel, Scene};
use chaya_fw::display::DisplayService;
use chaya_fw::heart::HeartCounters;
use embassy_futures::select::{select3, Either3};
use embassy_time::{Duration, Instant, Timer};
use esp_println::println;

async fn heart_snapshot() -> HeartCounters {
    HeartCounters {
        tx: *HEART_TX.lock().await,
        rx: *HEART_RX.lock().await,
        tx_baseline: *HEART_TX_BASE.lock().await,
        rx_baseline: *HEART_RX_BASE.lock().await,
        last_reset_day: *HEART_LAST_RESET_DAY.lock().await,
    }
}

/// Apply grace / shown-counter logic. Skips SPI when nothing visible changed (timer polls).
async fn paint_heart(panel: &mut EpdPanel, display: &mut DisplayService, force: bool) {
    let heart = heart_snapshot().await;
    let bat = (*BAT_PCT.lock().await).max(0);
    let mqtt = *MQTT_OK.lock().await;
    let wifi = *WIFI_OK.lock().await;
    let now_ms = (Instant::now().as_millis() & 0xffff_ffff) as u32;
    let prev_icon = display.state.last_icon;
    let prev_rx = display.state.last_drawn_rx;
    let prev_tx = display.state.last_drawn_tx;
    let Some(frame) = display.request_heart(
        false,
        wifi,
        mqtt,
        now_ms,
        DISPLAY_OFFLINE_GRACE_MS,
        heart.shown_rx(),
        heart.shown_tx(),
        bat,
    ) else {
        return;
    };
    if !force
        && frame.heart_icon == prev_icon
        && frame.shown_rx == prev_rx
        && frame.shown_tx == prev_tx
    {
        return;
    }
    let cracked = frame.heart_icon == DisplayHeartIcon::Crack;
    let _ = panel.show(&Scene::Heart {
        tx: frame.shown_tx,
        rx: frame.shown_rx,
        bat_pct: bat,
        cracked,
    });
}

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

    let mut display = DisplayService::default();
    display.set_content_allowed(true);

    // initial heart frame (grace starts if link already down)
    paint_heart(&mut panel, &mut display, true).await;

    loop {
        match select3(
            EPD_REDRAW.wait(),
            EPD_POWER_OFF.wait(),
            // Poll link grace so crack appears without waiting for another content event.
            Timer::after(Duration::from_secs(5)),
        )
        .await
        {
            Either3::First(()) => {
                Timer::after(Duration::from_secs(3)).await;
                while EPD_REDRAW.try_take().is_some() {}
                paint_heart(&mut panel, &mut display, true).await;
            }
            Either3::Second(()) => {
                println!("EPD: power off (soft-off)");
                let _ = panel.power_off();
                loop {
                    Timer::after(Duration::from_secs(3600)).await;
                }
            }
            Either3::Third(()) => {
                paint_heart(&mut panel, &mut display, false).await;
            }
        }
    }
}
