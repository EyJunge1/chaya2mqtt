//! Battery ADC poll task.
use chaya_fw::input::battery_adc::BatteryAdc;
use embassy_time::{Duration, Timer};
use crate::runtime::state::*;

#[embassy_executor::task]
pub async fn battery_task(mut bat: BatteryAdc) {
    loop {
        bat.poll();
        {
            let mut p = BAT_PCT.lock().await;
            *p = bat.last_pct;
            let mut m = BAT_MV.lock().await;
            *m = bat.last_mv;
        }
        EPD_REDRAW.signal(());
        Timer::after(Duration::from_secs(30)).await;
    }
}
