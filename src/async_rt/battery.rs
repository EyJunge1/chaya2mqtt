//! Battery ADC poll task.
use chaya_fw::battery::adc::BatteryAdc;
use embassy_time::{Duration, Timer};
use crate::async_rt::state::*;

#[embassy_executor::task]
pub async fn battery_task(mut bat: BatteryAdc) {
    let mut last_pct = i32::MIN;
    loop {
        bat.poll();
        {
            let mut p = BAT_PCT.lock().await;
            *p = bat.last_pct;
            let mut m = BAT_MV.lock().await;
            *m = bat.last_mv;
        }
        if bat.last_pct != last_pct {
            last_pct = bat.last_pct;
            sse_mark(chaya_pure::constants::SSE_DEVICE);
        }
        EPD_REDRAW.signal(());
        Timer::after(Duration::from_secs(30)).await;
    }
}
