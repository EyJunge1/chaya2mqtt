//! From `src/mqtt/*_pure.h`.

use crate::heart::heart_sent_remote_should_apply;

pub const MQTT_BROKER_MISSING_BACKOFF_MS: u32 = 60_000;
pub const MQTT_BACKOFF_INITIAL_MS: u32 = 30_000;
pub const MQTT_BACKOFF_MAX_MS: u32 = 60_000;
pub const MQTT_WIFI_DOWN_BACKOFF_MS: u32 = 20_000;
pub const MQTT_WIFI_LOST_DURING_TLS_BACKOFF_MS: u32 = 90_000;
pub const MQTT_NTP_RETRY_MS: u32 = 2_000;
pub const MQTT_PUBLISH_ACK_WAIT_MS: u32 = 5_000;
pub const MQTT_WIFI_NETWORK_TIMEOUT_MS: i32 = 5_000;
pub const MQTT_RECONNECT_TIMEOUT_MS: i32 = 60_000;

pub fn mqtt_settings_apply_should_clear_pending(has_unapplied_pending: bool) -> bool {
    !has_unapplied_pending
}

pub fn mqtt_settings_apply_should_finish(kill_ok: bool) -> bool {
    kill_ok
}

pub fn mqtt_settings_apply_should_defer(epd_active: bool, factory_queued: bool, shutdown: bool) -> bool {
    epd_active || factory_queued || shutdown
}

pub fn mqtt_settings_apply_nothing_pending_needs_retry(kill_coalesce_pending: bool) -> bool {
    kill_coalesce_pending
}

pub fn mqtt_event_is_live(
    ev_client: Option<*const ()>,
    live_client: Option<*const ()>,
    gen_snap: u32,
    gen_now: u32,
) -> bool {
    match (ev_client, live_client) {
        (Some(ev), Some(live)) => ev == live && gen_snap == gen_now,
        _ => false,
    }
}

pub fn mqtt_own_topic_tx_should_apply(
    incoming: i32,
    local_tx: i32,
    publish_in_flight: bool,
    pending_expected: i32,
) -> bool {
    if publish_in_flight && incoming <= pending_expected {
        return false;
    }
    heart_sent_remote_should_apply(incoming, local_tx)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn echo_ignored_while_publish_in_flight() {
        assert!(!mqtt_own_topic_tx_should_apply(5, 4, true, 5));
        assert!(mqtt_own_topic_tx_should_apply(6, 4, true, 5));
        assert!(mqtt_own_topic_tx_should_apply(5, 4, false, 5));
    }

    #[test]
    fn live_client_match() {
        let p = 0x1000 as *const ();
        assert!(mqtt_event_is_live(Some(p), Some(p), 1, 1));
        assert!(!mqtt_event_is_live(Some(p), Some(p), 1, 2));
        assert!(!mqtt_event_is_live(None, Some(p), 1, 1));
    }
}
