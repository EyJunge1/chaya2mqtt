//! From `src/async/system_shutdown_pure.h`.

/// Soft-off may latch-cut only when no OTA, shutdown, factory wipe, or in-flight admin apply owns the device.
pub fn soft_off_allowed(
    ota_busy: bool,
    shutdown: bool,
    factory_queued: bool,
    apply_in_flight: bool,
    mqtt_apply_pending: bool,
    settings_apply_pending: bool,
) -> bool {
    !ota_busy
        && !shutdown
        && !factory_queued
        && !apply_in_flight
        && !mqtt_apply_pending
        && !settings_apply_pending
}

/// Factory POST is busy when shutdown/factory already own, apply/OTA block, or a restart is armed.
pub fn factory_reset_http_blocked(
    shutdown: bool,
    factory_queued: bool,
    restart_blocked: bool,
    reboot_requested: bool,
    wifi_reconnect_requested: bool,
) -> bool {
    shutdown || factory_queued || restart_blocked || reboot_requested || wifi_reconnect_requested
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn soft_off_blocks_when_ota_busy() {
        assert!(!soft_off_allowed(true, false, false, false, false, false));
        assert!(soft_off_allowed(false, false, false, false, false, false));
    }

    #[test]
    fn factory_http_blocked_on_reboot_request() {
        assert!(factory_reset_http_blocked(false, false, false, true, false));
    }
}
