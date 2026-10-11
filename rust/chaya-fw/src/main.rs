//! Device firmware entry.
//! Build: `cargo +esp build -p chaya-fw --features device --release`

#![no_std]
#![no_main]

extern crate alloc;

mod boot;
mod async_rt;

use core::cell::RefCell;

use chaya_fw::audio::{AudioConfig, AudioHw};
use chaya_fw::hw::{battery_latch_on, display_power_enable, pins, sd_hold_off};
use chaya_fw::display::epd::EpdPanel;
use chaya_fw::battery::adc::BatteryAdc;
use chaya_fw::ota::OtaService;
use chaya_fw::config::SETUP_AP_SSID;
use chaya_fw::config::flash_kv::FlashKv;
use chaya_fw::config::flash_shared::FlashCell;
use chaya_fw::util::platform::GpioOut;
use chaya_fw::wifi::WlanService;
use chaya_fw::BootReport;
use embassy_executor::Spawner;
use embassy_sync::blocking_mutex::Mutex as BlockingMutex;
use embassy_sync::mutex::Mutex;
use esp_alloc as _;
use esp_backtrace as _;
use esp_hal::{
    clock::CpuClock,
    gpio::{Input, InputConfig, Level, Output, OutputConfig, Pull},
    ram,
    rng::Rng,
    timer::timg::TimerGroup,
};
use esp_println::println;
use esp_storage::FlashStorage;
use static_cell::StaticCell;

use crate::boot::util::leak;
use crate::async_rt::state::{
    AUDIO_CFG, HEART_RX, HEART_TX, KvMutex, OTA_STATUS,
};
use crate::async_rt::MonoClock;

esp_bootloader_esp_idf::esp_app_desc!();

macro_rules! mk_static {
    ($t:ty, $val:expr) => {{
        static STATIC_CELL: StaticCell<$t> = StaticCell::new();
        #[deny(unused_attributes)]
        let x = STATIC_CELL.uninit().write(($val));
        x
    }};
}

struct EarlyGpio {
    bat: Output<'static>,
    sd_clk: Output<'static>,
    sd_miso: Output<'static>,
    sd_mosi: Output<'static>,
    epd_en: Output<'static>,
}

impl GpioOut for EarlyGpio {
    fn set_output(&mut self, pin: u8, high: bool) {
        let level = if high { Level::High } else { Level::Low };
        match pin {
            p if p == pins::BAT_CONTROL => self.bat.set_level(level),
            p if p == pins::SD_CLK => self.sd_clk.set_level(level),
            p if p == pins::SD_MISO => self.sd_miso.set_level(level),
            p if p == pins::SD_MOSI => self.sd_mosi.set_level(level),
            p if p == pins::DISPLAY_PWR_EN => self.epd_en.set_level(level),
            _ => {}
        }
    }
}

#[esp_hal::main]
async fn main(spawner: Spawner) -> ! {
    let config = esp_hal::Config::default().with_cpu_clock(CpuClock::max());
    let peripherals = esp_hal::init(config);

    esp_alloc::heap_allocator!(#[ram(reclaimed)] size: 64 * 1024);
    // Keep headroom for the main stack (≥8 KiB esp-hal assert) after sleep/OTA statics.
    esp_alloc::heap_allocator!(size: 40 * 1024);

    let out_cfg = OutputConfig::default();
    let mut early = EarlyGpio {
        bat: Output::new(peripherals.GPIO17, Level::High, out_cfg),
        sd_clk: Output::new(peripherals.GPIO39, Level::Low, out_cfg),
        sd_miso: Output::new(peripherals.GPIO40, Level::Low, out_cfg),
        sd_mosi: Output::new(peripherals.GPIO41, Level::Low, out_cfg),
        epd_en: Output::new(peripherals.GPIO6, Level::High, out_cfg),
    };
    battery_latch_on(&mut early);
    sd_hold_off(&mut early);
    display_power_enable(&mut early, true);

    let timg0 = TimerGroup::new(peripherals.TIMG0);
    esp_rtos::start(timg0.timer0, peripherals.FROM_CPU_INTR0);

    let flash: &'static FlashCell = mk_static!(
        FlashCell,
        BlockingMutex::new(RefCell::new(FlashStorage::new(peripherals.FLASH)))
    );
    let mut kv = FlashKv::open(flash);
    let clock = MonoClock;
    let rng = Rng::new();
    let r0 = (rng.random() & 0xff) as u8;
    let r1 = (rng.random() & 0xff) as u8;
    let r2 = (rng.random() & 0xff) as u8;
    let (mut fw, report): (_, BootReport) =
        chaya_fw::boot(&mut early, &mut kv, &clock, [r0, r1, r2]);
    println!("=== Chaya2MQTT {} === id={}", report.version, report.device_id);
    println!("ap_mode={} heart_ready={}", report.ap_mode, report.heart_ready);

    {
        let mut t = HEART_TX.lock().await;
        *t = fw.heart.tx;
        let mut r = HEART_RX.lock().await;
        *r = fw.heart.rx;
    }

    let mut ap_entropy = [0u8; 20];
    for b in &mut ap_entropy {
        *b = (rng.random() & 0xff) as u8;
    }
    let ap_psk = WlanService::load_or_create_ap_psk(&mut kv, ap_entropy);
    let ap_psk = leak(ap_psk.as_str());

    {
        let ota = OtaService::load(&mut kv);
        let mut st = OTA_STATUS.lock().await;
        st.channel.clear();
        let _ = st.channel.push_str(ota.channel.as_str());
        let audio = AudioConfig::load(&mut kv);
        *AUDIO_CFG.lock().await = audio;
    }

    let kv: &'static KvMutex = mk_static!(KvMutex, Mutex::new(kv));

    let led = Output::new(peripherals.GPIO3, Level::High, out_cfg);
    let boot_btn = Input::new(
        peripherals.GPIO0,
        InputConfig::default().with_pull(Pull::Up),
    );
    let pwr_btn = Input::new(
        peripherals.GPIO18,
        InputConfig::default().with_pull(Pull::Up),
    );
    spawner
        .spawn(async_rt::button::button_led_task(boot_btn, pwr_btn, led, early.bat).unwrap());

    let bat = BatteryAdc::new(peripherals.ADC1, peripherals.GPIO4);
    spawner.spawn(async_rt::battery::battery_task(bat).unwrap());

    let audio = AudioHw::new(
        peripherals.I2C0,
        peripherals.GPIO47,
        peripherals.GPIO48,
        peripherals.GPIO42,
        peripherals.GPIO46,
        peripherals.GPIO14,
        peripherals.GPIO15,
        peripherals.GPIO38,
        peripherals.GPIO45,
        peripherals.I2S0,
        peripherals.DMA_CH0,
    );
    spawner.spawn(async_rt::audio::audio_task(audio).unwrap());
    spawner.spawn(async_rt::heart::heart_persist_task(kv).unwrap());
    spawner.spawn(async_rt::heart::reboot_task().unwrap());

    if let Some(panel) = EpdPanel::new(
        peripherals.SPI2,
        peripherals.GPIO12,
        peripherals.GPIO13,
        peripherals.GPIO11,
        peripherals.GPIO10,
        peripherals.GPIO9,
        peripherals.GPIO8,
    ) {
        spawner
            .spawn(async_rt::epd::epd_task(panel, report.ap_mode, SETUP_AP_SSID, ap_psk).unwrap());
    }

    let wifi = peripherals.WIFI;
    let seed = (rng.random() as u64) << 32 | rng.random() as u64;

    if report.ap_mode {
        println!("SoftAP PSK={ap_psk}");
        boot::run_softap(spawner, wifi, seed, kv, &report, ap_psk).await;
    } else {
        boot::run_sta(spawner, wifi, seed, kv, &mut fw, &report, flash).await;
    }
}
