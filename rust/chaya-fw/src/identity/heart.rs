//! Heart RX/TX counters + NVS (uses `chaya-pure`).

use crate::config::NS_CHAYA;
use crate::platform::KvStore;
use chaya_pure::heart::{
    heart_apply_allowed, heart_counter_shown_delta_pure, heart_sent_counter_next_pure,
    heart_sent_remote_should_apply,
};

const KEY_RX: &str = "rx";
const KEY_TX: &str = "tx";
const KEY_RX_BASE: &str = "rx_b";
const KEY_TX_BASE: &str = "tx_b";

#[derive(Clone, Debug, Default)]
pub struct HeartCounters {
    pub rx: i32,
    pub tx: i32,
    pub rx_baseline: i32,
    pub tx_baseline: i32,
}

impl HeartCounters {
    pub fn load(kv: &mut dyn KvStore) -> Self {
        Self {
            rx: kv.get_i32(NS_CHAYA, KEY_RX).unwrap_or(0),
            tx: kv.get_i32(NS_CHAYA, KEY_TX).unwrap_or(0),
            rx_baseline: kv.get_i32(NS_CHAYA, KEY_RX_BASE).unwrap_or(0),
            tx_baseline: kv.get_i32(NS_CHAYA, KEY_TX_BASE).unwrap_or(0),
        }
    }

    pub fn save(&self, kv: &mut dyn KvStore) -> bool {
        kv.put_i32(NS_CHAYA, KEY_RX, self.rx)
            && kv.put_i32(NS_CHAYA, KEY_TX, self.tx)
            && kv.put_i32(NS_CHAYA, KEY_RX_BASE, self.rx_baseline)
            && kv.put_i32(NS_CHAYA, KEY_TX_BASE, self.tx_baseline)
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
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::platform::SimKv;

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
        assert!(h.bump_tx_after_publish(false, false));
        assert_eq!(h.tx, 6);
    }
}
