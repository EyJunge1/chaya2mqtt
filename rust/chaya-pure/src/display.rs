//! From display `*_pure.h` and `view_state.h` helpers used by pure code.

pub use crate::constants::DISPLAY_OFFLINE_GRACE_MS;

pub const HEART_REDRAW_MIN_INTERVAL_MS: u32 = 20_000;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DisplayView {
    Unknown = 0,
    Heart = 1,
    SetupQr = 2,
    ProductTitle = 3,
    HeartCrack = 4,
    PowerOff = 5,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DisplayHeartIcon {
    Filled = 0,
    Crack = 1,
}

#[derive(Clone, Debug, Default)]
pub struct DisplayLinkState {
    pub outage_since_ms: u32,
}

pub fn display_link_is_online(ap_mode: bool, wifi_ok: bool, mqtt_ok: bool) -> bool {
    if ap_mode {
        true
    } else {
        wifi_ok && mqtt_ok
    }
}

pub fn display_heart_icon_decide(
    ap_mode: bool,
    wifi_ok: bool,
    mqtt_ok: bool,
    now_ms: u32,
    grace_ms: u32,
    st: &mut DisplayLinkState,
) -> DisplayHeartIcon {
    if display_link_is_online(ap_mode, wifi_ok, mqtt_ok) {
        st.outage_since_ms = 0;
        return DisplayHeartIcon::Filled;
    }
    if st.outage_since_ms == 0 {
        st.outage_since_ms = if now_ms == 0 { 1 } else { now_ms };
        return DisplayHeartIcon::Filled;
    }
    let down_for = now_ms.wrapping_sub(st.outage_since_ms);
    if down_for >= grace_ms {
        DisplayHeartIcon::Crack
    } else {
        DisplayHeartIcon::Filled
    }
}

pub fn display_view_for_heart_icon(icon: DisplayHeartIcon) -> DisplayView {
    match icon {
        DisplayHeartIcon::Crack => DisplayView::HeartCrack,
        DisplayHeartIcon::Filled => DisplayView::Heart,
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DisplayHeartRedrawDecision {
    SkipUnchanged = 0,
    QueueNow = 1,
    DeferPending = 2,
}

#[allow(clippy::too_many_arguments)]
pub fn display_heart_redraw_decide(
    current_rx: i32,
    current_tx: i32,
    last_drawn_rx: i32,
    last_drawn_tx: i32,
    icon_changed: bool,
    battery_icon_changed: bool,
    now_ms: u32,
    last_enqueue_ms: u32,
    min_interval_ms: u32,
    current_shown_rx: i32,
    current_shown_tx: i32,
    last_drawn_shown_rx: i32,
    last_drawn_shown_tx: i32,
) -> DisplayHeartRedrawDecision {
    if current_rx == last_drawn_rx
        && current_tx == last_drawn_tx
        && current_shown_rx == last_drawn_shown_rx
        && current_shown_tx == last_drawn_shown_tx
        && !icon_changed
        && !battery_icon_changed
    {
        return DisplayHeartRedrawDecision::SkipUnchanged;
    }
    if last_enqueue_ms != 0 && now_ms.wrapping_sub(last_enqueue_ms) < min_interval_ms {
        return DisplayHeartRedrawDecision::DeferPending;
    }
    DisplayHeartRedrawDecision::QueueNow
}

pub fn display_heart_skip_clears_pending(decision: DisplayHeartRedrawDecision) -> bool {
    decision == DisplayHeartRedrawDecision::SkipUnchanged
}

pub fn display_heart_skip_clears_pending_after_reread(
    first: DisplayHeartRedrawDecision,
    second: DisplayHeartRedrawDecision,
) -> bool {
    display_heart_skip_clears_pending(first) && display_heart_skip_clears_pending(second)
}

/// `u32::MAX` means wait forever (no pending work), matching C++ `ULONG_MAX`.
pub fn display_heart_redraw_wait_ms(
    now_ms: u32,
    last_enqueue_ms: u32,
    min_interval_ms: u32,
    pending: bool,
) -> u32 {
    if !pending {
        return u32::MAX;
    }
    if last_enqueue_ms == 0 {
        return 0;
    }
    let elapsed = now_ms.wrapping_sub(last_enqueue_ms);
    if elapsed >= min_interval_ms {
        0
    } else {
        min_interval_ms - elapsed
    }
}

#[allow(clippy::too_many_arguments)]
pub fn display_heart_needs_follow_up_redraw(
    drawn_rx: i32,
    drawn_tx: i32,
    current_rx: i32,
    current_tx: i32,
    icon_changed: bool,
    battery_icon_changed: bool,
    had_pending: bool,
    drawn_shown_rx: i32,
    drawn_shown_tx: i32,
    current_shown_rx: i32,
    current_shown_tx: i32,
) -> bool {
    had_pending
        || icon_changed
        || battery_icon_changed
        || current_rx != drawn_rx
        || current_tx != drawn_tx
        || current_shown_rx != drawn_shown_rx
        || current_shown_tx != drawn_shown_tx
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DisplayBatteryColor {
    Black,
    Yellow,
    Red,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DisplayBatteryIcon {
    Full = 0,
    Medium = 1,
    Low = 2,
    Empty = 3,
}

pub fn display_battery_icon(battery_pct: i32) -> DisplayBatteryIcon {
    if battery_pct >= 80 {
        DisplayBatteryIcon::Full
    } else if battery_pct >= 40 {
        DisplayBatteryIcon::Medium
    } else if battery_pct >= 15 {
        DisplayBatteryIcon::Low
    } else {
        DisplayBatteryIcon::Empty
    }
}

pub fn display_battery_color(battery_pct: i32) -> DisplayBatteryColor {
    match display_battery_icon(battery_pct) {
        DisplayBatteryIcon::Empty => DisplayBatteryColor::Red,
        DisplayBatteryIcon::Low => DisplayBatteryColor::Yellow,
        DisplayBatteryIcon::Full | DisplayBatteryIcon::Medium => DisplayBatteryColor::Black,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn crack_after_grace() {
        let mut st = DisplayLinkState::default();
        assert_eq!(
            display_heart_icon_decide(false, false, false, 100, 1000, &mut st),
            DisplayHeartIcon::Filled
        );
        assert_eq!(
            display_heart_icon_decide(false, false, false, 1200, 1000, &mut st),
            DisplayHeartIcon::Crack
        );
        assert_eq!(
            display_view_for_heart_icon(DisplayHeartIcon::Crack),
            DisplayView::HeartCrack
        );
        // C++ default grace is 5 minutes.
        assert_eq!(DISPLAY_OFFLINE_GRACE_MS, 300_000);
    }

    #[test]
    fn battery_thresholds() {
        assert_eq!(display_battery_icon(80), DisplayBatteryIcon::Full);
        assert_eq!(display_battery_icon(40), DisplayBatteryIcon::Medium);
        assert_eq!(display_battery_icon(15), DisplayBatteryIcon::Low);
        assert_eq!(display_battery_icon(14), DisplayBatteryIcon::Empty);
        assert_eq!(display_battery_color(10), DisplayBatteryColor::Red);
    }

    #[test]
    fn redraw_defer() {
        let d = display_heart_redraw_decide(
            1, 0, 0, 0, false, false, 100, 50, 200, 1, 0, 0, 0,
        );
        assert_eq!(d, DisplayHeartRedrawDecision::DeferPending);
    }
}
