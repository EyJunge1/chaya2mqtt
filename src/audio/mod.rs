//! ES8311 + I2S playback config (quiet hours from NVS).

use crate::config::NS_CFG;
use crate::util::platform::KvStore;
use crate::core::audio::{audio_playback_allowed, AudioKind};

const KEY_QUIET0: &str = "quiet0";
const KEY_QUIET1: &str = "quiet1";
const KEY_VOL: &str = "snd_vol";
const KEY_TX: &str = "snd_tx_en";
const KEY_RX: &str = "snd_rx_en";

#[derive(Clone, Debug)]
pub struct AudioConfig {
    pub volume: u8,
    pub tx_enabled: bool,
    pub rx_enabled: bool,
    pub quiet_start: u8,
    pub quiet_end: u8,
}

impl Default for AudioConfig {
    fn default() -> Self {
        Self::default_on()
    }
}

impl AudioConfig {
    pub fn default_on() -> Self {
        Self {
            volume: 70,
            tx_enabled: true,
            rx_enabled: true,
            quiet_start: 0,
            quiet_end: 0,
        }
    }

    pub fn load(kv: &mut dyn KvStore) -> Self {
        let mut cfg = Self::default_on();
        if let Some(v) = kv.get_i32(NS_CFG, KEY_VOL) {
            if (0..=100).contains(&v) {
                cfg.volume = v as u8;
            }
        }
        if let Some(v) = kv.get_i32(NS_CFG, KEY_TX) {
            cfg.tx_enabled = v != 0;
        }
        if let Some(v) = kv.get_i32(NS_CFG, KEY_RX) {
            cfg.rx_enabled = v != 0;
        }
        if let Some(v) = kv.get_i32(NS_CFG, KEY_QUIET0) {
            if (0..=23).contains(&v) {
                cfg.quiet_start = v as u8;
            }
        }
        if let Some(v) = kv.get_i32(NS_CFG, KEY_QUIET1) {
            if (0..=23).contains(&v) {
                cfg.quiet_end = v as u8;
            }
        }
        cfg
    }

    pub fn save(&self, kv: &mut dyn KvStore) -> bool {
        kv.put_i32(NS_CFG, KEY_VOL, self.volume as i32)
            && kv.put_i32(NS_CFG, KEY_TX, if self.tx_enabled { 1 } else { 0 })
            && kv.put_i32(NS_CFG, KEY_RX, if self.rx_enabled { 1 } else { 0 })
            && kv.put_i32(NS_CFG, KEY_QUIET0, self.quiet_start as i32)
            && kv.put_i32(NS_CFG, KEY_QUIET1, self.quiet_end as i32)
    }
}

#[derive(Clone, Debug)]
pub struct AudioService {
    pub cfg: AudioConfig,
}

impl Default for AudioService {
    fn default() -> Self {
        Self {
            cfg: AudioConfig::default_on(),
        }
    }
}

impl AudioService {
    pub fn may_play(&self, kind: AudioKind, time_synced: bool, hour: u8) -> bool {
        let enabled = match kind {
            AudioKind::Tx => self.cfg.tx_enabled,
            AudioKind::Rx => self.cfg.rx_enabled,
        };
        audio_playback_allowed(
            enabled,
            self.cfg.volume,
            time_synced,
            hour,
            self.cfg.quiet_start,
            self.cfg.quiet_end,
        )
    }
}

pub fn audio_backend_status() -> &'static str {
    "device: ES8311 I2C + PA + I2S DMA TX/RX + quiet hours"
}

#[cfg(feature = "device")]
pub mod hw;
#[cfg(feature = "device")]
pub use hw::AudioHw;

#[cfg(test)]
mod tests {
    use super::*;
    use crate::util::platform::SimKv;

    #[test]
    fn tx_rx_flags_roundtrip() {
        let mut kv = SimKv::default();
        let mut cfg = AudioConfig::default_on();
        cfg.tx_enabled = false;
        cfg.rx_enabled = true;
        assert!(cfg.save(&mut kv));
        let loaded = AudioConfig::load(&mut kv);
        assert!(!loaded.tx_enabled);
        assert!(loaded.rx_enabled);
    }
}
