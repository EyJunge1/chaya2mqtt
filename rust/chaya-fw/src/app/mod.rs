//! Boot orchestration — mirrors `src/main.cpp` `setup()` order.

use crate::audio::AudioService;
use crate::battery::BatteryService;
use crate::hw::{battery_latch_on, sd_hold_off};
use crate::button::ButtonService;
use crate::config::RuntimeFlags;
use crate::display::DisplayService;
use crate::heart::HeartCounters;
use crate::identity::DeviceId;
use crate::led::LedService;
use crate::mqtt::MqttService;
use crate::network::NetworkService;
use crate::ota::OtaService;
use crate::util::platform::{ClockMs, GpioOut, KvStore};
use crate::config::APP_VERSION;
use crate::web::WebAdminState;
use crate::wifi::{WlanMode, WlanService};

/// Aggregate firmware state after boot.
pub struct Firmware {
    pub flags: RuntimeFlags,
    pub device_id: DeviceId,
    pub heart: HeartCounters,
    pub wlan: WlanService,
    pub mqtt: MqttService,
    pub display: DisplayService,
    pub web: WebAdminState,
    pub ota: OtaService,
    pub audio: AudioService,
    pub battery: BatteryService,
    pub button: ButtonService,
    pub led: LedService,
    pub network: NetworkService,
}

#[derive(Clone, Debug)]
pub struct BootReport {
    pub version: &'static str,
    pub device_id: heapless::String<8>,
    pub ap_mode: bool,
    pub heart_ready: bool,
    pub backends: BackendsReport,
}

#[derive(Clone, Debug)]
pub struct BackendsReport {
    pub radio: &'static str,
    pub mqtt: &'static str,
    pub http: &'static str,
    pub panel: &'static str,
    pub ota: &'static str,
    pub audio: &'static str,
}

/// Full boot sequence (hardware I/O via traits; RF/EPD not live yet).
pub fn boot(
    gpio: &mut dyn GpioOut,
    kv: &mut dyn KvStore,
    _clock: &dyn ClockMs,
    random3: [u8; 3],
) -> (Firmware, BootReport) {
    battery_latch_on(gpio);
    sd_hold_off(gpio);

    let device_id = DeviceId::load_or_create(kv, random3);
    let heart = HeartCounters::load(kv);
    let mut wlan = WlanService::boot_from_nvs(kv);
    let mqtt = MqttService::load(kv);
    let ota = OtaService::load(kv);
    let mut display = DisplayService::default();
    let web = WebAdminState::default();
    let audio = AudioService::default();
    let battery = BatteryService::default();
    let button = ButtonService::default();
    let mut led = LedService::default();
    let network = NetworkService::default();

    let ap_mode = matches!(wlan.mode, WlanMode::SoftApSetup);
    if ap_mode {
        wlan.arm_setup_ap();
        display.set_content_allowed(false);
        let _splash = display.request_splash();
    }

    let heart_ready = mqtt.cfg.heart_ready();
    display.set_content_allowed(!ap_mode && heart_ready);

    led.startup_blink();

    let mut flags = RuntimeFlags {
        ap_mode,
        ..Default::default()
    };
    flags.wifi_ok = false;
    flags.mqtt_ok = false;

    let mut id_s = heapless::String::new();
    let _ = id_s.push_str(device_id.as_str());

    let report = BootReport {
        version: APP_VERSION,
        device_id: id_s,
        ap_mode,
        heart_ready,
        backends: BackendsReport {
            radio: crate::wifi::radio_backend_status(),
            mqtt: crate::mqtt::transport_backend_status(),
            http: crate::web::http_backend_status(),
            panel: crate::display::panel_backend_status(),
            ota: crate::ota::ota_backend_status(),
            audio: crate::audio::audio_backend_status(),
        },
    };

    let fw = Firmware {
        flags,
        device_id,
        heart,
        wlan,
        mqtt,
        display,
        web,
        ota,
        audio,
        battery,
        button,
        led,
        network,
    };
    (fw, report)
}

/// Request a heart send (button / admin) — enqueues net publish when paired.
pub fn request_send(fw: &mut Firmware) -> bool {
    if !fw.flags.soft_off_allowed() {
        return false;
    }
    if !fw.mqtt.cfg.heart_ready() {
        return false;
    }
    fw.network
        .enqueue(crate::network::NetCmd::ChayaPublish)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::util::platform::{SimClock, SimGpio, SimKv};

    #[test]
    fn boot_softap_without_wifi_nvs() {
        let mut gpio = SimGpio::default();
        let mut kv = SimKv::default();
        let clock = SimClock { ms: 0 };
        let (fw, report) = boot(&mut gpio, &mut kv, &clock, [0x11, 0x22, 0x33]);
        assert!(report.ap_mode);
        assert!(!report.heart_ready);
        assert_eq!(fw.device_id.as_str(), "112233");
        assert_eq!(gpio.get(crate::hw::pins::BAT_CONTROL), Some(true));
        assert!(report.backends.radio.contains("SoftAP"));
    }
}
