//! MQTT heart transport (TCP + optional TLS when port ≥ 8883).

use crate::config::NS_MQTT;
use crate::util::platform::KvStore;
use crate::core::constants::DEVICE_ID_HEX_LEN;
use crate::core::mqtt::{
    mqtt_own_topic_tx_should_apply, MQTT_BACKOFF_INITIAL_MS, MQTT_PUBLISH_ACK_WAIT_MS,
};

const KEY_HOST: &str = "host";
const KEY_PORT: &str = "port";
const KEY_USER: &str = "user";
const KEY_PASS: &str = "pass";
const KEY_PARTNER: &str = "partner";
const KEY_PAIRING: &str = "pairing";

#[derive(Clone, Debug, Default)]
pub struct MqttConfig {
    pub host: heapless::String<64>,
    pub port: u16,
    pub user: heapless::String<32>,
    pub pass: heapless::String<64>,
    pub partner_id: heapless::String<8>,
    pub pairing_id: heapless::String<8>,
}

impl MqttConfig {
    pub fn load(kv: &mut dyn KvStore) -> Self {
        let mut cfg = Self {
            port: 8883,
            ..Default::default()
        };
        let mut buf = [0u8; 64];
        if let Some(n) = kv.get_blob(NS_MQTT, KEY_HOST, &mut buf) {
            let _ = cfg.host.push_str(core::str::from_utf8(&buf[..n]).unwrap_or(""));
        }
        if let Some(p) = kv.get_i32(NS_MQTT, KEY_PORT) {
            if p > 0 && p <= 65535 {
                cfg.port = p as u16;
            }
        }
        if let Some(n) = kv.get_blob(NS_MQTT, KEY_USER, &mut buf) {
            let _ = cfg.user.push_str(core::str::from_utf8(&buf[..n.min(32)]).unwrap_or(""));
        }
        if let Some(n) = kv.get_blob(NS_MQTT, KEY_PASS, &mut buf) {
            let _ = cfg.pass.push_str(core::str::from_utf8(&buf[..n]).unwrap_or(""));
        }
        let mut id = [0u8; 8];
        if let Some(n) = kv.get_blob(NS_MQTT, KEY_PARTNER, &mut id) {
            let _ = cfg
                .partner_id
                .push_str(core::str::from_utf8(&id[..n.min(DEVICE_ID_HEX_LEN)]).unwrap_or(""));
        }
        if let Some(n) = kv.get_blob(NS_MQTT, KEY_PAIRING, &mut id) {
            let _ = cfg
                .pairing_id
                .push_str(core::str::from_utf8(&id[..n.min(DEVICE_ID_HEX_LEN)]).unwrap_or(""));
        }
        cfg
    }

    pub fn broker_configured(&self) -> bool {
        !self.host.is_empty()
    }

    pub fn heart_ready(&self) -> bool {
        self.broker_configured() && self.partner_id.len() == DEVICE_ID_HEX_LEN
    }

    pub fn pub_topic(&self, fallback_device_id: &str) -> heapless::String<48> {
        let id = if self.pairing_id.len() == DEVICE_ID_HEX_LEN {
            self.pairing_id.as_str()
        } else {
            fallback_device_id
        };
        let mut t = heapless::String::new();
        let _ = t.push_str("chaya2mqtt/");
        let _ = t.push_str(id);
        t
    }

    /// Partner heart topic `chaya2mqtt/<partner>` — empty when unpaired.
    pub fn sub_topic(&self) -> heapless::String<48> {
        let mut t = heapless::String::new();
        if self.partner_id.len() == DEVICE_ID_HEX_LEN {
            let _ = t.push_str("chaya2mqtt/");
            let _ = t.push_str(self.partner_id.as_str());
        }
        t
    }

    /// Per-device LWT topic `chaya2mqtt/<deviceId>/lwt` (never the shared heart topic).
    pub fn lwt_topic(&self, device_id: &str) -> heapless::String<48> {
        let _ = self;
        let mut t = heapless::String::new();
        let _ = t.push_str("chaya2mqtt/");
        let _ = t.push_str(device_id);
        let _ = t.push_str("/lwt");
        t
    }

    pub fn save(&self, kv: &mut dyn KvStore) -> bool {
        kv.put_blob(NS_MQTT, KEY_HOST, self.host.as_bytes())
            && kv.put_i32(NS_MQTT, KEY_PORT, self.port as i32)
            && kv.put_blob(NS_MQTT, KEY_USER, self.user.as_bytes())
            && kv.put_blob(NS_MQTT, KEY_PASS, self.pass.as_bytes())
            && kv.put_blob(NS_MQTT, KEY_PARTNER, self.partner_id.as_bytes())
            && kv.put_blob(NS_MQTT, KEY_PAIRING, self.pairing_id.as_bytes())
    }
}

#[derive(Clone, Debug, Default)]
pub struct MqttService {
    pub cfg: MqttConfig,
    pub connected: bool,
    pub publish_in_flight: bool,
    pub pending_expected: i32,
}

impl MqttService {
    pub fn load(kv: &mut dyn KvStore) -> Self {
        Self {
            cfg: MqttConfig::load(kv),
            ..Default::default()
        }
    }

    pub fn should_apply_own_topic_tx(&self, incoming: i32, local_tx: i32) -> bool {
        mqtt_own_topic_tx_should_apply(
            incoming,
            local_tx,
            self.publish_in_flight,
            self.pending_expected,
        )
    }

    pub fn backoff_initial_ms() -> u32 {
        MQTT_BACKOFF_INITIAL_MS
    }

    pub fn publish_ack_wait_ms() -> u32 {
        MQTT_PUBLISH_ACK_WAIT_MS
    }
}

pub fn transport_backend_status() -> &'static str {
    #[cfg(all(feature = "device", feature = "mbedtls"))]
    {
        "device: MQTT 3.1.1 TCP + TLS (mbedtls-rs CA) when port>=8883"
    }
    #[cfg(all(feature = "device", not(feature = "mbedtls")))]
    {
        "device: MQTT 3.1.1 TCP + TLS1.3 when port>=8883 (host DER pin)"
    }
    #[cfg(not(feature = "device"))]
    {
        "host: MQTT config/proto only (no TLS transport)"
    }
}

/// Packet builders/parsers are host-testable (no device HAL).
pub mod proto;
#[cfg(feature = "device")]
pub mod session;
