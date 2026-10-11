//! Build HttpShared and run edge-http.

use chaya_fw::web::{self as http_server, HttpShared};
use chaya_fw::BootReport;
use embassy_net::Stack;
use static_cell::StaticCell;

use crate::async_rt::state::KvMutex;

macro_rules! mk_static {
    ($t:ty, $val:expr) => {{
        static STATIC_CELL: StaticCell<$t> = StaticCell::new();
        let x = STATIC_CELL.uninit().write(($val));
        x
    }};
}

pub async fn start_http(
    stack: Stack<'static>,
    kv: &'static KvMutex,
    report: &BootReport,
    ap_mode: bool,
    ap_psk: Option<&'static str>,
) -> ! {
    let shared: &'static HttpShared = mk_static!(
        HttpShared,
        HttpShared {
            kv,
            device_id: crate::boot::util::leak(report.device_id.as_str()),
            ap_mode,
            ap_psk,
            heart_tx: &crate::async_rt::state::HEART_TX,
            heart_rx: &crate::async_rt::state::HEART_RX,
            mqtt_ok: &crate::async_rt::state::MQTT_OK,
            bat_pct: &crate::async_rt::state::BAT_PCT,
            bat_mv: &crate::async_rt::state::BAT_MV,
            audio_cfg: &crate::async_rt::state::AUDIO_CFG,
            ota_status: &crate::async_rt::state::OTA_STATUS,
            wifi_scan: &crate::async_rt::state::WIFI_SCAN,
            wifi_pending: &crate::async_rt::state::WIFI_PENDING,
            wifi_test_state: &crate::async_rt::state::WIFI_TEST_STATE,
            wifi_test_ssid: &crate::async_rt::state::WIFI_TEST_SSID,
            wifi_test_owns: &crate::async_rt::state::WIFI_TEST_OWNS,
            sta_ssid: &crate::async_rt::state::STA_SSID,
            sta_rssi: &crate::async_rt::state::STA_RSSI,
            stack,
            heart_bump: &crate::async_rt::state::HEART_BUMP,
            audio_beep: &crate::async_rt::state::AUDIO_BEEP,
            heart_dirty: &crate::async_rt::state::HEART_DIRTY,
            epd_redraw: &crate::async_rt::state::EPD_REDRAW,
            reboot: &crate::async_rt::state::REBOOT,
            ota_check: &crate::async_rt::state::OTA_CHECK,
            ota_install: &crate::async_rt::state::OTA_INSTALL,
            scan_kick: &crate::async_rt::state::SCAN_KICK,
            wifi_test_kick: &crate::async_rt::state::WIFI_TEST_KICK,
        }
    );
    http_server::run_admin_http(shared).await
}

