//! Heart persist + periodic baseline reset + reboot tasks.
use chaya_fw::config::UiPrefs;
use chaya_fw::heart::HeartCounters;
use chaya_pure::heart::{calendar_day_since_epoch_utc, ntp_time_looks_synced};
use embassy_futures::select::{select, Either};
use embassy_time::{Duration, Timer};
use esp_println::println;
use crate::async_rt::state::*;

async fn snapshot_heart_from_runtime() -> HeartCounters {
    HeartCounters {
        tx: *HEART_TX.lock().await,
        rx: *HEART_RX.lock().await,
        tx_baseline: *HEART_TX_BASE.lock().await,
        rx_baseline: *HEART_RX_BASE.lock().await,
        last_reset_day: *HEART_LAST_RESET_DAY.lock().await,
    }
}

async fn publish_heart_to_runtime(heart: &HeartCounters) {
    *HEART_TX.lock().await = heart.tx;
    *HEART_RX.lock().await = heart.rx;
    *HEART_TX_BASE.lock().await = heart.tx_baseline;
    *HEART_RX_BASE.lock().await = heart.rx_baseline;
    *HEART_LAST_RESET_DAY.lock().await = heart.last_reset_day;
}

async fn persist_heart(kv: &KvMutex, heart: &HeartCounters) -> bool {
    let mut kv = kv.lock().await;
    heart.save(&mut *kv)
}

/// Persist dirty counters and periodically apply `resetDays` baseline resets (C++ intent).
#[embassy_executor::task]
pub async fn heart_persist_task(kv: &'static KvMutex) {
    loop {
        match select(
            HEART_DIRTY.wait(),
            Timer::after(Duration::from_secs(2)),
        )
        .await
        {
            Either::First(()) => {
                // coalesce bursts
                Timer::after(Duration::from_millis(80)).await;
                while HEART_DIRTY.try_take().is_some() {}
                let heart = snapshot_heart_from_runtime().await;
                if persist_heart(kv, &heart).await {
                    println!("HEART: persisted tx={} rx={}", heart.tx, heart.rx);
                }
                EPD_REDRAW.signal(());
            }
            Either::Second(()) => {
                let unix = {
                    let clock = WALL_CLOCK.lock().await;
                    clock.unix_now()
                };
                let Some(unix) = unix.filter(|&u| ntp_time_looks_synced(u)) else {
                    continue;
                };
                let period_days = {
                    let mut kv = kv.lock().await;
                    UiPrefs::load(&mut *kv).reset_days
                };
                let current_day = calendar_day_since_epoch_utc(unix);
                let mut heart = snapshot_heart_from_runtime().await;
                let old_rx_b = heart.rx_baseline;
                let old_tx_b = heart.tx_baseline;
                let period_changed = heart.maybe_periodically_reset(period_days, current_day);
                let cap_changed = heart.maybe_reset_baselines_when_capped();
                if !(period_changed || cap_changed) {
                    continue;
                }
                publish_heart_to_runtime(&heart).await;
                sse_mark(chaya_pure::constants::SSE_CHAYA);
                if persist_heart(kv, &heart).await {
                    let baselines_moved =
                        heart.rx_baseline != old_rx_b || heart.tx_baseline != old_tx_b;
                    if baselines_moved && period_days > 0 && period_changed {
                        println!(
                            "HEART: periodic display reset ({} days) day={}",
                            period_days, current_day
                        );
                    } else if baselines_moved {
                        println!("HEART: display baseline reset (cap)");
                    } else {
                        println!("HEART: reset-day anchor={}", heart.last_reset_day);
                    }
                }
                EPD_REDRAW.signal(());
            }
        }
    }
}

#[embassy_executor::task]
pub async fn reboot_task() {
    REBOOT.wait().await;
    Timer::after(Duration::from_millis(400)).await;
    println!("REBOOT");
    esp_hal::system::software_reset();
}
