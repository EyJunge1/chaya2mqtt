//! OTA check/install task.
use chaya_fw::ota::client::{self as ota_client, EspRng as OtaRng};
use chaya_fw::config::flash_shared::FlashCell;
use embassy_net::Stack;
use esp_println::println;
use crate::async_rt::state::*;

#[embassy_executor::task]
pub async fn ota_task(stack: Stack<'static>, mut rng: OtaRng, flash: &'static FlashCell) {
    loop {
        match embassy_futures::select::select(OTA_CHECK.wait(), OTA_INSTALL.wait()).await {
            embassy_futures::select::Either::First(()) => {
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
                let _ = ota_client::check_latest(stack, &mut rng, &mut local).await;
                let mut st = OTA_STATUS.lock().await;
                *st = local;
            }
            embassy_futures::select::Either::Second(()) => {
                let mut local = {
                    let st = OTA_STATUS.lock().await;
                    st.clone()
                };
                {
                    let mut st = OTA_STATUS.lock().await;
                    st.installing = true;
                }
                let ok = ota_client::install_latest(stack, &mut rng, flash, &mut local).await;
                {
                    let mut st = OTA_STATUS.lock().await;
                    *st = local;
                }
                if ok {
                    println!("OTA: rebooting into new image");
                    REBOOT.signal(());
                }
            }
        }
    }
}
