//! NTP sync task.
use chaya_fw::network::ntp::{self, WallClock};
use embassy_net::Stack;
use embassy_time::{Duration, Timer};
use crate::async_rt::state::*;

#[embassy_executor::task]
pub async fn ntp_task(stack: Stack<'static>) {
    Timer::after(Duration::from_secs(2)).await;
    loop {
        let mut clock = WallClock::default();
        if ntp::sync_once(stack, &mut clock).await {
            *WALL_CLOCK.lock().await = clock;
            *TIME_SYNCED.lock().await = true;
            Timer::after(Duration::from_secs(6 * 3600)).await;
        } else {
            Timer::after(Duration::from_secs(60)).await;
        }
    }
}
