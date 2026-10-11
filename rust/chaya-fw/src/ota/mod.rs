//! OTA from GitHub Releases + SHA-512 (flash backend + GitHub client pending).

use crate::config::NS_CFG;
use crate::util::platform::KvStore;
use chaya_pure::ota::ota_github_check_may_clear_pending_release;

const KEY_CHANNEL: &str = "upd_chan";

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
        if s == "beta" {
            Self::Beta
        } else {
            Self::Stable
        }
    }
}

#[derive(Clone, Debug, Default)]
pub struct OtaService {
    pub channel: OtaChannel,
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
        Self {
            channel,
            ..Default::default()
        }
    }

    pub fn save_channel(&self, kv: &mut dyn KvStore) -> bool {
        kv.put_blob(NS_CFG, KEY_CHANNEL, self.channel.as_str().as_bytes())
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
