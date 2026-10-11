//! ES8311 + I2S playback (codec crate + I2S stabilize pending).

use chaya_pure::audio::{audio_playback_allowed, AudioKind};

#[derive(Clone, Debug, Default)]
pub struct AudioConfig {
    pub volume: u8,
    pub tx_enabled: bool,
    pub rx_enabled: bool,
    pub quiet_start: u8,
    pub quiet_end: u8,
}

impl AudioConfig {
    pub fn default_on() -> Self {
        Self {
            volume: 50,
            tx_enabled: true,
            rx_enabled: true,
            quiet_start: 0,
            quiet_end: 0,
        }
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
    "device: ES8311 I2C + PA beep; I2S DMA sample push still TODO"
}
