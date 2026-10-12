//! From `src/heart/counter_pure.h` + periodic baseline reset (C++ `counter_sync.cpp`).

use super::constants::{
    DISPLAY_COUNTER_MAX, HEART_LAST_RESET_DAY_UNSET, NTP_MIN_VALID_UTC_EPOCH,
};

/// Non-negative display delta: max(0, raw - baseline).
pub fn heart_counter_delta_pure(raw: i32, baseline: i32) -> i32 {
    if raw > baseline {
        raw - baseline
    } else {
        0
    }
}

/// UTC calendar day since Unix epoch (C++ `calendarDaySinceEpochUtc`).
pub fn calendar_day_since_epoch_utc(unix: u64) -> u32 {
    (unix / 86_400) as u32
}

pub fn ntp_time_looks_synced(unix: u64) -> bool {
    unix >= NTP_MIN_VALID_UTC_EPOCH
}

/// Outcome of `maybePeriodicallyResetCounters` decide step (no I/O).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum HeartPeriodicResetDecide {
    /// Nothing to do (period 0, or not yet due).
    None,
    /// First NTP-synced run: persist today's anchor only (do not touch baselines).
    AnchorDay(u32),
    /// Anchor was in the future — clamp to today (baselines unchanged).
    ClampAnchor(u32),
    /// Period elapsed: set baselines to absolute counters and re-anchor to `day`.
    ResetBaselines { day: u32 },
}

/// Decide periodic display-counter reset. Absolute MQTT counters are never wiped —
/// only baselines move so `shown = raw - baseline` restarts at 0.
pub fn heart_periodic_reset_decide(
    last_reset_day: u32,
    current_day: u32,
    period_days: u8,
) -> HeartPeriodicResetDecide {
    if last_reset_day == HEART_LAST_RESET_DAY_UNSET {
        return HeartPeriodicResetDecide::AnchorDay(current_day);
    }
    if period_days == 0 {
        return HeartPeriodicResetDecide::None;
    }
    let mut last_day = last_reset_day;
    let mut clamped = false;
    if last_day > current_day.saturating_add(1) {
        last_day = current_day;
        clamped = true;
    }
    let days_since = current_day.saturating_sub(last_day);
    if days_since >= u32::from(period_days) {
        HeartPeriodicResetDecide::ResetBaselines { day: current_day }
    } else if clamped {
        HeartPeriodicResetDecide::ClampAnchor(current_day)
    } else {
        HeartPeriodicResetDecide::None
    }
}

/// True when shown delta would reach the display cap (C++ `maybeResetDisplayBaselinesWhenCapped`).
pub fn heart_display_baseline_needs_cap_reset(raw: i32, baseline: i32) -> bool {
    i64::from(raw) - i64::from(baseline) >= i64::from(DISPLAY_COUNTER_MAX)
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

    #[test]
    fn periodic_reset_anchor_then_due() {
        assert_eq!(
            heart_periodic_reset_decide(HEART_LAST_RESET_DAY_UNSET, 100, 7),
            HeartPeriodicResetDecide::AnchorDay(100)
        );
        assert_eq!(
            heart_periodic_reset_decide(100, 106, 7),
            HeartPeriodicResetDecide::None
        );
        assert_eq!(
            heart_periodic_reset_decide(100, 107, 7),
            HeartPeriodicResetDecide::ResetBaselines { day: 107 }
        );
        assert_eq!(
            heart_periodic_reset_decide(100, 200, 0),
            HeartPeriodicResetDecide::None
        );
        assert_eq!(
            heart_periodic_reset_decide(500, 100, 7),
            HeartPeriodicResetDecide::ClampAnchor(100)
        );
    }

    #[test]
    fn cap_reset_threshold() {
        assert!(!heart_display_baseline_needs_cap_reset(998, 0));
        assert!(heart_display_baseline_needs_cap_reset(999, 0));
    }
}
