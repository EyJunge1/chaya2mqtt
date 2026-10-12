//! OTA from GitHub Releases + SHA-512 (GitHub client + esp-hal-ota flash).

use crate::config::NS_CFG;
use crate::util::platform::KvStore;
use chaya_pure::ota::ota_github_check_may_clear_pending_release;

const KEY_CHANNEL: &str = "upd_chan";
const KEY_UPD_DAY: &str = "upd_day";

#[derive(Clone, Copy, Debug, PartialEq, Eq, Default)]
pub enum OtaChannel {
    #[default]
    Stable,
    Beta,
}

impl OtaChannel {
    pub fn as_str(self) -> &'static str {
        match self {
            Self::Stable => "stable",
            Self::Beta => "beta",
        }
    }

    pub fn parse(s: &str) -> Self {
        Self::try_parse(s).unwrap_or(Self::Stable)
    }

    pub fn try_parse(s: &str) -> Option<Self> {
        match s {
            "stable" => Some(Self::Stable),
            "beta" => Some(Self::Beta),
            _ => None,
        }
    }
}

#[derive(Clone, Debug, Default)]
pub struct OtaService {
    pub channel: OtaChannel,
    /// UTC calendar day of last successful GitHub check (`cfg/upd_day`).
    pub last_check_day: u32,
    pub install_queued: bool,
    pub busy: bool,
}

impl OtaService {
    pub fn load(kv: &mut dyn KvStore) -> Self {
        let mut buf = [0u8; 16];
        let channel = if let Some(n) = kv.get_blob(NS_CFG, KEY_CHANNEL, &mut buf) {
            OtaChannel::parse(core::str::from_utf8(&buf[..n]).unwrap_or("stable"))
        } else {
            OtaChannel::Stable
        };
        let last_check_day = kv.get_i32(NS_CFG, KEY_UPD_DAY).unwrap_or(0).max(0) as u32;
        Self {
            channel,
            last_check_day,
            ..Default::default()
        }
    }

    pub fn save_channel(&self, kv: &mut dyn KvStore) -> bool {
        kv.put_blob(NS_CFG, KEY_CHANNEL, self.channel.as_str().as_bytes())
    }

    pub fn save_last_check_day(&mut self, kv: &mut dyn KvStore, day_utc: u32) -> bool {
        self.last_check_day = day_utc;
        kv.put_i32(NS_CFG, KEY_UPD_DAY, day_utc as i32)
    }

    pub fn may_clear_pending_release(&self) -> bool {
        ota_github_check_may_clear_pending_release(self.install_queued)
    }
}

pub fn ota_backend_status() -> &'static str {
    "device: GitHub OTA check + SHA-512 verified esp-hal-ota flash"
}

#[cfg(feature = "device")]
pub mod client;
#[cfg(feature = "device")]
pub use client::{EspRng, OtaStatus};
#[cfg(feature = "device")]
pub mod flash;
