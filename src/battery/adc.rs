//! Pack voltage via GPIO4 ADC (÷2 divider → ×2).

use esp_hal::{
    analog::adc::{Adc, AdcConfig, Attenuation},
    peripherals::{ADC1, GPIO4},
};
use esp_println::println;
use nb::block;

const SAMPLES: usize = 8;
const DIVIDER: i32 = 2;

pub struct BatteryAdc {
    adc: Adc<'static, ADC1<'static>, esp_hal::Blocking>,
    pin: esp_hal::analog::adc::AdcPin<GPIO4<'static>, ADC1<'static>>,
    pub last_mv: i32,
    pub last_pct: i32,
}

impl BatteryAdc {
    pub fn new(adc1: ADC1<'static>, gpio4: GPIO4<'static>) -> Self {
        let mut cfg = AdcConfig::new();
        let pin = cfg.enable_pin(gpio4, Attenuation::_11dB);
        let adc = Adc::new(adc1, cfg);
        Self {
            adc,
            pin,
            last_mv: 0,
            last_pct: 0,
        }
    }

    /// Average raw → approximate millivolts at pin, then × divider for pack.
    pub fn poll(&mut self) -> i32 {
        let mut sum: u32 = 0;
        let mut n = 0u32;
        for _ in 0..SAMPLES {
            if let Ok(v) = block!(self.adc.read_oneshot(&mut self.pin)) {
                sum += v as u32;
                n += 1;
            }
        }
        if n == 0 {
            return self.last_mv;
        }
        // 12-bit @ 11dB ≈ 0–3100 mV full-scale (datasheet ballpark)
        let avg = sum / n;
        let pin_mv = ((avg as u32) * 3100) / 4095;
        let pack_mv = (pin_mv as i32) * DIVIDER;
        self.last_mv = pack_mv;
        self.last_pct = crate::core::battery::battery_pct_from_milli_volts(pack_mv);
        println!("BAT: {pack_mv} mV ({}%)", self.last_pct);
        pack_mv
    }
}
