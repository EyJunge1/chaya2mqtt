//! From `src/ota/ota_queue_pure.h`.

/// `runGithubCheck` may replace the copied release only when no install is queued.
pub fn ota_github_check_may_clear_pending_release(install_queued: bool) -> bool {
    !install_queued
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn clear_only_when_idle() {
        assert!(ota_github_check_may_clear_pending_release(false));
        assert!(!ota_github_check_may_clear_pending_release(true));
    }
}
