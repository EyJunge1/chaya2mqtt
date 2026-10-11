//! NTP sync task.
use chaya_fw::network::ntp::{self, WallClock};
use embassy_net::Stack;
use embassy_time::{Duration, Timer};
use crate::async_rt::state::*;

#[embassy_executor::task]
pub async fn ntp_task(stack: Stack<'static>, ntp1: &'static str, ntp2: &'static str) {
    Timer::after(Duration::from_secs(2)).await;
    let hosts = [ntp1, ntp2];
    loop {
        let mut clock = WallClock::default();
        if ntp::sync_once(stack, &mut clock, &hosts).await {
            *WALL_CLOCK.lock().await = clock;
            *TIME_SYNCED.lock().await = true;
            Timer::after(Duration::from_secs(6 * 3600)).await;
        } else {
            Timer::after(Duration::from_secs(60)).await;
        }
    }
}
