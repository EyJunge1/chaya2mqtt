//! Admin HTTP + SSE tick helpers (edge-http server owns the live transport).

use crate::core::web::{
    mqtt_page_conn, sse_tick_mask_for_ap_mode, sse_tick_select_bits, web_admin_restart_blocked,
};

#[derive(Clone, Debug, Default)]
pub struct WebAdminState {
    pub pending_sse_bits: u32,
    pub last_sse_work_ms: u32,
}

impl WebAdminState {
    pub fn restart_blocked(
        &self,
        ota_busy: bool,
        mqtt_apply_pending: bool,
        settings_apply_pending: bool,
        mqtt_apply_unqueued: bool,
        apply_in_flight: bool,
    ) -> bool {
        web_admin_restart_blocked(
            ota_busy,
            mqtt_apply_pending,
            settings_apply_pending,
            mqtt_apply_unqueued,
            apply_in_flight,
        )
    }

    pub fn sse_tick(&mut self, now_ms: u32, keepalive_ms: u32, ap_mode: bool) -> (u32, bool) {
        let (bits, ka) = sse_tick_select_bits(
            self.pending_sse_bits,
            now_ms,
            self.last_sse_work_ms,
            keepalive_ms,
        );
        let bits = sse_tick_mask_for_ap_mode(bits, ap_mode);
        if bits != 0 {
            self.pending_sse_bits = 0;
            self.last_sse_work_ms = now_ms;
        }
        (bits, ka)
    }

    pub fn mqtt_status_line(configured: bool, connected: bool) -> bool {
        mqtt_page_conn(configured, connected)
    }
}

pub fn http_backend_status() -> &'static str {
    "device: embedded SPA + OpenAPI + SSE + captive probes"
}
