//! From `src/wifi/wlan_recovery.h`.

pub const WLAN_RECOVERY_LINK_DOWN_GRACE_MS: u32 = 45_000;
pub const WLAN_RECOVERY_REASSOC_COOLDOWN_MS: u32 = 30_000;
pub const WLAN_RECOVERY_RESTART_AFTER_MS: u32 = 600_000;
pub const WLAN_RECOVERY_MIN_UPTIME_BEFORE_RESTART_MS: u32 = 900_000;
pub const WLAN_RECOVERY_MAX_RESTARTS_PER_DAY: u8 = 3;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum WlanRecoveryAction {
    None,
    ForcedReassoc,
    Restart,
}

#[derive(Clone, Copy, Debug, Default)]
pub struct WlanRecoveryState {
    pub link_down_since_ms: u32,
    pub last_forced_reassoc_ms: u32,
}

/// Pure recovery decision (C++ `wlanRecoveryDecide`).
pub fn wlan_recovery_decide(
    ap_mode: bool,
    sta_connected_ok: bool,
    ota_blocking: bool,
    has_sta_credentials: bool,
    now_ms: u32,
    uptime_ms: u32,
    st: &mut WlanRecoveryState,
    restarts_used_today: u8,
    max_restarts_per_day: u8,
) -> WlanRecoveryAction {
    if ap_mode || !has_sta_credentials {
        st.link_down_since_ms = 0;
        return WlanRecoveryAction::None;
    }
    if sta_connected_ok {
        st.link_down_since_ms = 0;
        return WlanRecoveryAction::None;
    }

    if st.link_down_since_ms == 0 {
        st.link_down_since_ms = if now_ms == 0 { 1 } else { now_ms };
        return WlanRecoveryAction::None;
    }

    if ota_blocking {
        return WlanRecoveryAction::None;
    }

    let down_for = now_ms.wrapping_sub(st.link_down_since_ms);

    if uptime_ms >= WLAN_RECOVERY_MIN_UPTIME_BEFORE_RESTART_MS
        && down_for >= WLAN_RECOVERY_RESTART_AFTER_MS
    {
        if restarts_used_today < max_restarts_per_day {
            return WlanRecoveryAction::Restart;
        }
        if st.last_forced_reassoc_ms == 0
            || now_ms.wrapping_sub(st.last_forced_reassoc_ms) >= WLAN_RECOVERY_REASSOC_COOLDOWN_MS
        {
            st.last_forced_reassoc_ms = if now_ms == 0 { 1 } else { now_ms };
            return WlanRecoveryAction::ForcedReassoc;
        }
        return WlanRecoveryAction::None;
    }

    if down_for >= WLAN_RECOVERY_LINK_DOWN_GRACE_MS
        && (st.last_forced_reassoc_ms == 0
            || now_ms.wrapping_sub(st.last_forced_reassoc_ms) >= WLAN_RECOVERY_REASSOC_COOLDOWN_MS)
    {
        st.last_forced_reassoc_ms = if now_ms == 0 { 1 } else { now_ms };
        return WlanRecoveryAction::ForcedReassoc;
    }

    WlanRecoveryAction::None
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn reassoc_after_grace() {
        let mut st = WlanRecoveryState::default();
        assert_eq!(
            wlan_recovery_decide(false, false, false, true, 1, 0, &mut st, 0, 3),
            WlanRecoveryAction::None
        );
        assert_eq!(
            wlan_recovery_decide(false, false, false, true, 45_001, 0, &mut st, 0, 3),
            WlanRecoveryAction::ForcedReassoc
        );
    }

    #[test]
    fn clear_on_connect() {
        let mut st = WlanRecoveryState {
            link_down_since_ms: 10,
            last_forced_reassoc_ms: 0,
        };
        assert_eq!(
            wlan_recovery_decide(false, true, false, true, 100, 0, &mut st, 0, 3),
            WlanRecoveryAction::None
        );
        assert_eq!(st.link_down_since_ms, 0);
    }
}
