//! Net runner + mDNS.

use chaya_fw::network::mdns as mdns_svc;
use embassy_net::{Runner, Stack};
use esp_radio::wifi::Interface;

#[embassy_executor::task]
pub async fn mdns_task(stack: Stack<'static>, hostname: &'static str) {
    mdns_svc::run_mdns(stack, hostname).await
}

#[embassy_executor::task]
pub async fn net_task(mut runner: Runner<'static, Interface>) {
    runner.run().await
}
