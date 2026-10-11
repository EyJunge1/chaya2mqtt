//! Build HttpShared and run edge-http.

use chaya_fw::http::{self as http_server, HttpShared};
use chaya_fw::BootReport;
use embassy_net::Stack;
use static_cell::StaticCell;

use crate::runtime::state::KvMutex;

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
            heart_tx: &crate::runtime::state::HEART_TX,
            heart_rx: &crate::runtime::state::HEART_RX,
            mqtt_ok: &crate::runtime::state::MQTT_OK,
            bat_pct: &crate::runtime::state::BAT_PCT,
            bat_mv: &crate::runtime::state::BAT_MV,
            audio_cfg: &crate::runtime::state::AUDIO_CFG,
            ota_status: &crate::runtime::state::OTA_STATUS,
            wifi_scan: &crate::runtime::state::WIFI_SCAN,
            wifi_pending: &crate::runtime::state::WIFI_PENDING,
            wifi_test_state: &crate::runtime::state::WIFI_TEST_STATE,
            wifi_test_ssid: &crate::runtime::state::WIFI_TEST_SSID,
            wifi_test_owns: &crate::runtime::state::WIFI_TEST_OWNS,
            sta_ssid: &crate::runtime::state::STA_SSID,
            sta_rssi: &crate::runtime::state::STA_RSSI,
            stack,
            heart_bump: &crate::runtime::state::HEART_BUMP,
            audio_beep: &crate::runtime::state::AUDIO_BEEP,
            heart_dirty: &crate::runtime::state::HEART_DIRTY,
            epd_redraw: &crate::runtime::state::EPD_REDRAW,
            reboot: &crate::runtime::state::REBOOT,
            ota_check: &crate::runtime::state::OTA_CHECK,
            ota_install: &crate::runtime::state::OTA_INSTALL,
            scan_kick: &crate::runtime::state::SCAN_KICK,
            wifi_test_kick: &crate::runtime::state::WIFI_TEST_KICK,
        }
    );
    http_server::run_admin_http(shared).await
}

