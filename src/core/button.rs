//! From `src/button/button_debounce_pure.h` and `button_soft_off_pure.h`.

/// Shared GPIO debounce fields (host-testable).
#[derive(Clone, Debug, Default)]
pub struct DebouncedGpioState {
    pub last_raw_reading: i32,
    pub last_debounce_change_ms: u32,
    pub debounced_level: i32,
}

/// Update debounce state from a raw GPIO sample. Level commits after `stable_ms` without change.
pub fn debounce_update(state: &mut DebouncedGpioState, raw: i32, now_ms: u32, stable_ms: u32) {
    if raw != state.last_raw_reading {
        state.last_raw_reading = raw;
        state.last_debounce_change_ms = now_ms;
    }
    if now_ms.wrapping_sub(state.last_debounce_change_ms) >= stable_ms {
        state.debounced_level = state.last_raw_reading;
    }
}

/// EXT1 ANY_LOW is level-triggered. Arm only when PWR is not already LOW.
pub fn soft_off_may_arm_ext1_wake(pwr_level: i32) -> bool {
    pwr_level != 0
}

/// Continuous HIGH timer for the pre-EXT1 bounce settle.
#[derive(Clone, Debug, Default)]
pub struct SoftOffReleaseSettle {
    pub high_since_ms: u32,
}

/// True once `pwr_level` has been inactive (not LOW) for `settle_ms`. Resets on LOW.
pub fn soft_off_release_settled(
    st: &mut SoftOffReleaseSettle,
    pwr_level: i32,
    now_ms: u32,
    settle_ms: u32,
) -> bool {
    if !soft_off_may_arm_ext1_wake(pwr_level) {
        st.high_since_ms = 0;
        return false;
    }
    if st.high_since_ms == 0 {
        st.high_since_ms = now_ms;
        return false;
    }
    now_ms.wrapping_sub(st.high_since_ms) >= settle_ms
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn debounce_commits_after_stable() {
        let mut st = DebouncedGpioState::default();
        debounce_update(&mut st, 1, 0, 50);
        assert_eq!(st.debounced_level, 0);
        debounce_update(&mut st, 1, 50, 50);
        assert_eq!(st.debounced_level, 1);
    }

    #[test]
    fn soft_off_settle() {
        let mut st = SoftOffReleaseSettle::default();
        assert!(!soft_off_release_settled(&mut st, 1, 10, 100));
        assert!(!soft_off_release_settled(&mut st, 1, 50, 100));
        assert!(soft_off_release_settled(&mut st, 1, 110, 100));
    }
}
