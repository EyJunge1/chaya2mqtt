//! Heart RX/TX counters + NVS (uses `crate::core`).

use crate::config::NS_CHAYA;
use crate::util::platform::KvStore;
use crate::core::constants::HEART_LAST_RESET_DAY_UNSET;
use crate::core::heart::{
    heart_apply_allowed, heart_counter_shown_delta_pure, heart_display_baseline_needs_cap_reset,
    heart_periodic_reset_decide, heart_sent_counter_next_pure, heart_sent_remote_should_apply,
    HeartPeriodicResetDecide,
};

const KEY_RX: &str = "rx";
const KEY_TX: &str = "tx";
const KEY_RX_BASE: &str = "rx_b";
const KEY_TX_BASE: &str = "tx_b";
const KEY_RST_DAY: &str = "rst_d";

#[derive(Clone, Debug)]
pub struct HeartCounters {
    pub rx: i32,
    pub tx: i32,
    pub rx_baseline: i32,
    pub tx_baseline: i32,
    /// UTC calendar day of last display-counter reset; `HEART_LAST_RESET_DAY_UNSET` if none.
    pub last_reset_day: u32,
}

impl Default for HeartCounters {
    fn default() -> Self {
        Self {
            rx: 0,
            tx: 0,
            rx_baseline: 0,
            tx_baseline: 0,
            last_reset_day: HEART_LAST_RESET_DAY_UNSET,
        }
    }
}

impl HeartCounters {
    pub fn load(kv: &mut dyn KvStore) -> Self {
        let last_reset_day = match kv.get_i32(NS_CHAYA, KEY_RST_DAY) {
            Some(v) if v >= 0 => v as u32,
            _ => HEART_LAST_RESET_DAY_UNSET,
        };
        Self {
            rx: kv.get_i32(NS_CHAYA, KEY_RX).unwrap_or(0),
            tx: kv.get_i32(NS_CHAYA, KEY_TX).unwrap_or(0),
            rx_baseline: kv.get_i32(NS_CHAYA, KEY_RX_BASE).unwrap_or(0),
            tx_baseline: kv.get_i32(NS_CHAYA, KEY_TX_BASE).unwrap_or(0),
            last_reset_day,
        }
    }

    pub fn save(&self, kv: &mut dyn KvStore) -> bool {
        let rst = if self.last_reset_day == HEART_LAST_RESET_DAY_UNSET {
            -1
        } else {
            self.last_reset_day as i32
        };
        kv.put_i32(NS_CHAYA, KEY_RX, self.rx)
            && kv.put_i32(NS_CHAYA, KEY_TX, self.tx)
            && kv.put_i32(NS_CHAYA, KEY_RX_BASE, self.rx_baseline)
            && kv.put_i32(NS_CHAYA, KEY_TX_BASE, self.tx_baseline)
            && kv.put_i32(NS_CHAYA, KEY_RST_DAY, rst)
    }

    pub fn shown_rx(&self) -> i32 {
        heart_counter_shown_delta_pure(self.rx, self.rx_baseline)
    }

    pub fn shown_tx(&self) -> i32 {
        heart_counter_shown_delta_pure(self.tx, self.tx_baseline)
    }

    pub fn bump_tx_after_publish(&mut self, shutdown: bool, nvs_suspended: bool) -> bool {
        if !heart_apply_allowed(shutdown, nvs_suspended) {
            return false;
        }
        self.tx = heart_sent_counter_next_pure(self.tx);
        true
    }

    pub fn apply_remote_tx(&mut self, incoming: i32, shutdown: bool, nvs_suspended: bool) -> bool {
        if !heart_apply_allowed(shutdown, nvs_suspended) {
            return false;
        }
        if !heart_sent_remote_should_apply(incoming, self.tx) {
            return false;
        }
        self.tx = incoming;
        true
    }

    pub fn apply_remote_rx(&mut self, incoming: i32, shutdown: bool, nvs_suspended: bool) -> bool {
        if !heart_apply_allowed(shutdown, nvs_suspended) {
            return false;
        }
        if incoming <= self.rx {
            return false;
        }
        self.rx = incoming;
        true
    }

    /// Apply C++ `maybePeriodicallyResetCounters` semantics. Returns true if state changed.
    /// Does **not** clear absolute `rx`/`tx` — only baselines / reset-day anchor.
    pub fn maybe_periodically_reset(&mut self, period_days: u8, current_day: u32) -> bool {
        match heart_periodic_reset_decide(self.last_reset_day, current_day, period_days) {
            HeartPeriodicResetDecide::None => false,
            HeartPeriodicResetDecide::AnchorDay(day) | HeartPeriodicResetDecide::ClampAnchor(day) => {
                self.last_reset_day = day;
                true
            }
            HeartPeriodicResetDecide::ResetBaselines { day } => {
                self.rx_baseline = self.rx;
                self.tx_baseline = self.tx;
                self.last_reset_day = day;
                true
            }
        }
    }

    /// C++ `maybeResetDisplayBaselinesWhenCapped` — roll baselines when shown hits cap.
    pub fn maybe_reset_baselines_when_capped(&mut self) -> bool {
        let mut changed = false;
        if heart_display_baseline_needs_cap_reset(self.rx, self.rx_baseline) {
            self.rx_baseline = self.rx;
            changed = true;
        }
        if heart_display_baseline_needs_cap_reset(self.tx, self.tx_baseline) {
            self.tx_baseline = self.tx;
            changed = true;
        }
        changed
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::util::platform::SimKv;

    #[test]
    fn persist_roundtrip() {
        let mut kv = SimKv::default();
        let mut h = HeartCounters {
            rx: 3,
            tx: 5,
            ..Default::default()
        };
        assert!(h.save(&mut kv));
        let loaded = HeartCounters::load(&mut kv);
        assert_eq!(loaded.rx, 3);
        assert_eq!(loaded.tx, 5);
        assert_eq!(loaded.last_reset_day, HEART_LAST_RESET_DAY_UNSET);
        assert!(h.bump_tx_after_publish(false, false));
        assert_eq!(h.tx, 6);
    }

    #[test]
    fn periodic_reset_moves_baselines_only() {
        let mut h = HeartCounters {
            rx: 40,
            tx: 50,
            rx_baseline: 10,
            tx_baseline: 20,
            last_reset_day: 100,
        };
        assert_eq!(h.shown_rx(), 30);
        assert!(h.maybe_periodically_reset(7, 107));
        assert_eq!(h.rx, 40);
        assert_eq!(h.tx, 50);
        assert_eq!(h.rx_baseline, 40);
        assert_eq!(h.tx_baseline, 50);
        assert_eq!(h.shown_rx(), 0);
        assert_eq!(h.shown_tx(), 0);
        assert_eq!(h.last_reset_day, 107);
    }

    #[test]
    fn rst_day_persists() {
        let mut kv = SimKv::default();
        let h = HeartCounters {
            last_reset_day: 12_345,
            ..Default::default()
        };
        assert!(h.save(&mut kv));
        let loaded = HeartCounters::load(&mut kv);
        assert_eq!(loaded.last_reset_day, 12_345);
    }
}
