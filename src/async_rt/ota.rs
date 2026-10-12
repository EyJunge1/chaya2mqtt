//! OTA check/install task + once-per-UTC-day automatic GitHub probe (STA).
use chaya_fw::config::APP_VERSION;
use chaya_fw::config::flash_shared::FlashCell;
use chaya_fw::ota::OtaService;
use chaya_fw::ota::client::{self as ota_client, EspRng as OtaRng};
use chaya_pure::heart::{calendar_day_since_epoch_utc, ntp_time_looks_synced};
use chaya_pure::ota::ota_daily_check_due;
use embassy_futures::select::{select3, Either3};
use embassy_net::Stack;
use embassy_time::{Duration, Timer};
use esp_println::println;
use crate::async_rt::state::*;

const DAILY_POLL_SECS: u64 = 60;

async fn mark_check_day_if_synced(kv: &KvMutex) {
    let unix = {
        let clock = WALL_CLOCK.lock().await;
        clock.unix_now()
    };
    let Some(unix) = unix.filter(|&u| ntp_time_looks_synced(u)) else {
        return;
    };
    let day = calendar_day_since_epoch_utc(unix);
    let mut kv = kv.lock().await;
    let mut ota = OtaService::load(&mut *kv);
    if ota.save_last_check_day(&mut *kv, day) {
        println!("OTA: upd_day={day}");
    }
}

async fn run_github_check(
    stack: Stack<'static>,
    rng: &mut OtaRng,
    automatic: bool,
) -> bool {
    let mut local = {
        let st = OTA_STATUS.lock().await;
        let mut local = st.clone();
        local.checking = true;
        local
    };
    {
        let mut st = OTA_STATUS.lock().await;
        st.checking = true;
    }
    sse_mark(chaya_pure::constants::SSE_OTA);
    println!(
        "OTA: {} update check started (channel={})",
        if automatic { "Automatic" } else { "Manual" },
        if local.channel.is_empty() {
            "stable"
        } else {
            local.channel.as_str()
        }
    );
    let ok = ota_client::check_latest(stack, rng, &mut local).await;
    {
        let mut st = OTA_STATUS.lock().await;
        *st = local;
    }
    sse_mark(chaya_pure::constants::SSE_OTA);
    ok
}

async fn maybe_daily_check(stack: Stack<'static>, rng: &mut OtaRng, kv: &KvMutex) {
    let sta_ok = *WIFI_OK.lock().await || {
        let ssid = STA_SSID.lock().await;
        !ssid.is_empty()
    };
    let busy = {
        let st = OTA_STATUS.lock().await;
        st.checking || st.installing
    };
    let unix = {
        let clock = WALL_CLOCK.lock().await;
        clock.unix_now()
    };
    let ntp_ok = unix.is_some_and(ntp_time_looks_synced);
    let today = unix
        .filter(|&u| ntp_time_looks_synced(u))
        .map(calendar_day_since_epoch_utc)
        .unwrap_or(0);
    let last_day = {
        let mut kv = kv.lock().await;
        OtaService::load(&mut *kv).last_check_day
    };
    if !ota_daily_check_due(
        APP_VERSION,
        sta_ok,
        false, // ota_task only runs in STA boot
        ntp_ok,
        busy,
        last_day,
        today,
    ) {
        return;
    }
    let ok = run_github_check(stack, rng, true).await;
    if ok {
        mark_check_day_if_synced(kv).await;
    }
}

#[embassy_executor::task]
pub async fn ota_task(
    stack: Stack<'static>,
    mut rng: OtaRng,
    flash: &'static FlashCell,
    kv: &'static KvMutex,
) {
    loop {
        match select3(
            OTA_CHECK.wait(),
            OTA_INSTALL.wait(),
            Timer::after(Duration::from_secs(DAILY_POLL_SECS)),
        )
        .await
        {
            Either3::First(()) => {
                let ok = run_github_check(stack, &mut rng, false).await;
                if ok {
                    // Manual check in STA with NTP also advances the daily guard.
                    let sta_ok = *WIFI_OK.lock().await || {
                        let ssid = STA_SSID.lock().await;
                        !ssid.is_empty()
                    };
                    if sta_ok {
                        mark_check_day_if_synced(kv).await;
                    }
                }
            }
            Either3::Second(()) => {
                let mut local = {
                    let st = OTA_STATUS.lock().await;
                    st.clone()
                };
                {
                    let mut st = OTA_STATUS.lock().await;
                    st.installing = true;
                }
                sse_mark(chaya_pure::constants::SSE_OTA);
                let ok = ota_client::install_latest(stack, &mut rng, flash, &mut local).await;
                {
                    let mut st = OTA_STATUS.lock().await;
                    *st = local;
                }
                sse_mark(chaya_pure::constants::SSE_OTA);
                if ok {
                    println!("OTA: rebooting into new image");
                    REBOOT.signal(());
                }
            }
            Either3::Third(()) => {
                maybe_daily_check(stack, &mut rng, kv).await;
            }
        }
    }
}
