//! From `src/heart/counter_pure.h`.

use crate::constants::DISPLAY_COUNTER_MAX;

/// Non-negative display delta: max(0, raw - baseline).
pub fn heart_counter_delta_pure(raw: i32, baseline: i32) -> i32 {
    if raw > baseline {
        raw - baseline
    } else {
        0
    }
}

/// Format-side capped delta; returns `DISPLAY_COUNTER_MAX + 1` as "999+" sentinel.
pub fn heart_counter_shown_delta_pure(raw: i32, baseline: i32) -> i32 {
    let delta64 = i64::from(raw) - i64::from(baseline);
    let shown64 = delta64.clamp(0, 9999);
    if shown64 > i64::from(DISPLAY_COUNTER_MAX) {
        DISPLAY_COUNTER_MAX + 1
    } else {
        shown64 as i32
    }
}

pub fn heart_counter_should_show_plus_pure(raw: i32, baseline: i32) -> bool {
    heart_counter_shown_delta_pure(raw, baseline) > DISPLAY_COUNTER_MAX
}

pub fn heart_apply_allowed(shutdown: bool, nvs_suspended: bool) -> bool {
    !shutdown && !nvs_suspended
}

pub fn heart_sent_apply_if_allowed(
    dest: &mut i32,
    expected: i32,
    shutdown: bool,
    nvs_suspended: bool,
) -> bool {
    if !heart_apply_allowed(shutdown, nvs_suspended) {
        return false;
    }
    *dest = expected;
    true
}

pub fn heart_sent_counter_next_pure(current: i32) -> i32 {
    if current >= i32::MAX {
        i32::MAX
    } else {
        current + 1
    }
}

pub fn heart_sent_remote_should_apply(incoming: i32, local_tx: i32) -> bool {
    incoming > local_tx
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn shown_delta_plus_sentinel() {
        assert_eq!(heart_counter_shown_delta_pure(2000, 0), 1000);
        assert!(heart_counter_should_show_plus_pure(2000, 0));
        assert_eq!(heart_counter_delta_pure(5, 10), 0);
    }

    #[test]
    fn remote_tx_only_forward() {
        assert!(heart_sent_remote_should_apply(5, 4));
        assert!(!heart_sent_remote_should_apply(4, 4));
    }
}
