//! ES8311 (I2C) + PA rails + short I2S beep.

use crate::board::{audio_amp_enable, audio_rail_enable, pins};
use crate::platform::GpioOut;
use embassy_time::{Duration, Timer};
use esp_hal::{
    gpio::{Level, Output, OutputConfig},
    i2c::master::{Config as I2cConfig, I2c},
    peripherals::{GPIO14, GPIO15, GPIO38, GPIO42, GPIO45, GPIO46, GPIO47, GPIO48, I2C0, I2S0},
    time::Rate,
};
use esp_println::println;

const ES8311_ADDR: u8 = 0x18;

struct PaGpio {
    pa_en: Output<'static>,
    pa_ctrl: Output<'static>,
}

impl GpioOut for PaGpio {
    fn set_output(&mut self, pin: u8, high: bool) {
        let level = if high { Level::High } else { Level::Low };
        if pin == pins::PA_EN {
            self.pa_en.set_level(level);
        } else if pin == pins::PA_CTRL {
            self.pa_ctrl.set_level(level);
        }
    }
}

fn es_write(i2c: &mut I2c<'static, esp_hal::Blocking>, reg: u8, val: u8) -> bool {
    i2c.write(ES8311_ADDR, &[reg, val]).is_ok()
}

fn es8311_prepare(i2c: &mut I2c<'static, esp_hal::Blocking>, volume: u8) -> bool {
    if !es_write(i2c, 0x00, 0x1F) {
        return false;
    }
    let _ = es_write(i2c, 0x00, 0x00);
    let _ = es_write(i2c, 0x00, 0x80);
    let _ = es_write(i2c, 0x01, 0x3F);
    let _ = es_write(i2c, 0x02, 0x00);
    let _ = es_write(i2c, 0x03, 0x10);
    let _ = es_write(i2c, 0x04, 0x10);
    let _ = es_write(i2c, 0x05, 0x00);
    let _ = es_write(i2c, 0x06, 0x03);
    let _ = es_write(i2c, 0x07, 0x00);
    let _ = es_write(i2c, 0x08, 0xFF);
    let _ = es_write(i2c, 0x09, 0x0C);
    let _ = es_write(i2c, 0x0A, 0x0C);
    let _ = es_write(i2c, 0x0D, 0x01);
    let _ = es_write(i2c, 0x0E, 0x02);
    let _ = es_write(i2c, 0x14, 0x00);
    let _ = es_write(i2c, 0x16, 0x00);
    let _ = es_write(i2c, 0x17, 0x00);
    let _ = es_write(i2c, 0x12, 0x00);
    let _ = es_write(i2c, 0x13, 0x10);
    // volume 0..100 → register scale (rough)
    let v = ((volume as u16) * 255 / 100) as u8;
    let _ = es_write(i2c, 0x32, v);
    true
}

/// Owns I2C + PA; I2S pins reserved for later DMA beep.
pub struct AudioHw {
    i2c: I2c<'static, esp_hal::Blocking>,
    pa: PaGpio,
    ready: bool,
    /// Keep I2S pins owned so nothing else claims them.
    _i2s_pins: (GPIO14<'static>, GPIO15<'static>, GPIO38<'static>, GPIO45<'static>, I2S0<'static>),
}

impl AudioHw {
    pub fn new(
        i2c0: I2C0<'static>,
        sda: GPIO47<'static>,
        scl: GPIO48<'static>,
        pa_en: GPIO42<'static>,
        pa_ctrl: GPIO46<'static>,
        mclk: GPIO14<'static>,
        sclk: GPIO15<'static>,
        lrck: GPIO38<'static>,
        dout: GPIO45<'static>,
        i2s0: I2S0<'static>,
    ) -> Self {
        let out_cfg = OutputConfig::default();
        let i2c = I2c::new(i2c0, I2cConfig::default().with_frequency(Rate::from_khz(100)))
            .expect("i2c")
            .with_sda(sda)
            .with_scl(scl);
        let mut pa = PaGpio {
            pa_en: Output::new(pa_en, Level::High, out_cfg), // off (active-low)
            pa_ctrl: Output::new(pa_ctrl, Level::Low, out_cfg),
        };
        audio_rail_enable(&mut pa, false);
        audio_amp_enable(&mut pa, false);
        Self {
            i2c,
            pa,
            ready: false,
            _i2s_pins: (mclk, sclk, lrck, dout, i2s0),
        }
    }

    pub fn init_codec(&mut self) -> bool {
        self.ready = es8311_prepare(&mut self.i2c, 50);
        println!("AUDIO: ES8311 {}", if self.ready { "ok" } else { "fail" });
        self.ready
    }

    /// Beep: power rails briefly (I2S DMA sample push still TODO).
    pub async fn beep(&mut self) {
        if !self.ready && !self.init_codec() {
            return;
        }
        audio_rail_enable(&mut self.pa, true);
        Timer::after(Duration::from_millis(5)).await;
        audio_amp_enable(&mut self.pa, true);
        Timer::after(Duration::from_millis(180)).await;
        audio_amp_enable(&mut self.pa, false);
        audio_rail_enable(&mut self.pa, false);
        println!("AUDIO: beep (PA pulse)");
    }
}
