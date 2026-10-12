//! App / NVS config namespaces (parity with docs + `nvs_keys.h`).

pub const NS_WIFI: &str = "wifi";
pub const NS_MQTT: &str = "mqtt";
pub const NS_CFG: &str = "cfg";
pub const NS_CHAYA: &str = "chaya";

pub const SETUP_AP_SSID: &str = "Chaya2MQTT";
pub const SETUP_AP_IP: &str = "4.3.2.1";
pub const DEVICE_HOSTNAME: &str = "chaya2mqtt";
pub const STA_HOSTNAME_PREFIX: &str = "chaya2mqtt-";

#[derive(Clone, Debug, Default)]
pub struct RuntimeFlags {
    pub shutdown: bool,
    pub factory_queued: bool,
    pub ota_busy: bool,
    pub mqtt_apply_pending: bool,
    pub settings_apply_pending: bool,
    pub apply_in_flight: bool,
    pub ap_mode: bool,
    pub wifi_ok: bool,
    pub mqtt_ok: bool,
    pub nvs_chaya_suspended: bool,
}

impl RuntimeFlags {
    pub fn soft_off_allowed(&self) -> bool {
        crate::core::async_shutdown::soft_off_allowed(
            self.ota_busy,
            self.shutdown,
            self.factory_queued,
            self.apply_in_flight,
            self.mqtt_apply_pending,
            self.settings_apply_pending,
        )
    }
}
