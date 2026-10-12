//! Network task stand-in: Wi-Fi + MQTT loops share one service.

use crate::mqtt::MqttService;
use crate::wifi::{WlanMode, WlanService};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum NetCmd {
    MqttSettingsChanged,
    WifiGotIp,
    WifiReconnect,
    ChayaPublish,
    FactoryResetRequested,
}

#[derive(Default)]
pub struct NetworkService {
    pub pending: heapless::Vec<NetCmd, 8>,
}

impl NetworkService {
    pub fn enqueue(&mut self, cmd: NetCmd) -> bool {
        self.pending.push(cmd).is_ok()
    }

    pub fn drain_one(&mut self) -> Option<NetCmd> {
        if self.pending.is_empty() {
            None
        } else {
            Some(self.pending.remove(0))
        }
    }

    pub fn wifi_mqtt_link_ok(wlan: &WlanService, mqtt: &MqttService) -> (bool, bool) {
        let wifi_ok = matches!(wlan.mode, WlanMode::Sta) && wlan.connected;
        let mqtt_ok = mqtt.connected;
        (wifi_ok, mqtt_ok)
    }
}

#[cfg(feature = "device")]
pub mod ntp;
#[cfg(feature = "device")]
pub mod mdns;
