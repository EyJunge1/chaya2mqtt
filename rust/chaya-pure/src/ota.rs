//! From `src/ota/ota_queue_pure.h` and `ota_health.h`.

/// Stable STA runtime before cancelling OTA rollback (ms).
pub const OTA_HEALTH_STABLE_MS: u32 = 30_000;

/// `runGithubCheck` may replace the copied release only when no install is queued.
pub fn ota_github_check_may_clear_pending_release(install_queued: bool) -> bool {
    !install_queued
}

/// True once setup is complete, boot WiFi settled, STA linked, and `window_ms` elapsed
/// since `settled_at_ms` (wrap-safe via wrapping subtract).
pub fn ota_health_window_elapsed(
    setup_complete: bool,
    boot_settled: bool,
    sta_connected: bool,
    settled_at_ms: u32,
    now_ms: u32,
    window_ms: u32,
) -> bool {
    if !setup_complete || !boot_settled || !sta_connected || settled_at_ms == 0 {
        return false;
    }
    now_ms.wrapping_sub(settled_at_ms) >= window_ms
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn clear_only_when_idle() {
        assert!(ota_github_check_may_clear_pending_release(false));
        assert!(!ota_github_check_may_clear_pending_release(true));
    }

    #[test]
    fn health_needs_sta_and_window() {
        assert!(!ota_health_window_elapsed(true, true, false, 1, 40_000, OTA_HEALTH_STABLE_MS));
        assert!(!ota_health_window_elapsed(true, true, true, 0, 40_000, OTA_HEALTH_STABLE_MS));
        assert!(ota_health_window_elapsed(true, true, true, 1, 30_001, OTA_HEALTH_STABLE_MS));
    }
}
