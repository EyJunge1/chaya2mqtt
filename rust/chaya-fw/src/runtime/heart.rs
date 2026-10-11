//! Heart persist + reboot tasks.
use chaya_fw::identity::HeartCounters;
use embassy_time::{Duration, Timer};
use esp_println::println;
use crate::runtime::state::*;

#[embassy_executor::task]
pub async fn heart_persist_task(kv: &'static KvMutex) {
    loop {
        HEART_DIRTY.wait().await;
        // coalesce bursts
        Timer::after(Duration::from_millis(80)).await;
        while HEART_DIRTY.try_take().is_some() {}
        let tx = *HEART_TX.lock().await;
        let rx = *HEART_RX.lock().await;
        let mut kv = kv.lock().await;
        let mut heart = HeartCounters::load(&mut *kv);
        heart.tx = tx;
        heart.rx = rx;
        if heart.save(&mut *kv) {
            println!("HEART: persisted tx={tx} rx={rx}");
        }
        EPD_REDRAW.signal(());
    }
}


#[embassy_executor::task]
pub async fn reboot_task() {
    REBOOT.wait().await;
    Timer::after(Duration::from_millis(400)).await;
    println!("REBOOT");
    esp_hal::system::software_reset();
}
