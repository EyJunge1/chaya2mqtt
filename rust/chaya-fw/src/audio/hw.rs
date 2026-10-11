//! ES8311 (I2C via `es8311` crate) + PA rails + I2S DMA beep samples (TX/RX tones).

use crate::board::{audio_amp_enable, audio_rail_enable, pins};
use crate::storage::platform::GpioOut;
use chaya_pure::audio::AudioKind;
use embassy_time::{Duration, Timer};
use es8311::{ClockConfig, Es8311, Resolution};
use esp_hal::{
    delay::Delay,
    dma_tx_buffer,
    gpio::{Level, Output, OutputConfig},
    i2c::master::{Config as I2cConfig, I2c},
    i2s::master::{Channels, DataFormat, I2s, I2sTx, TdmConfig},
    peripherals::{DMA_CH0, GPIO14, GPIO15, GPIO38, GPIO42, GPIO45, GPIO46, GPIO47, GPIO48, I2C0, I2S0},
    time::Rate,
    Blocking,
};
use esp_hal::dma::DmaTxBuf;
use esp_println::println;

const ES8311_ADDR: u8 = 0x18;
const SAMPLE_RATE: u32 = 24_000;
/// ESP I2S MCLK is typically 256 × fs for this board.
const MCLK_HZ: u32 = SAMPLE_RATE * 256;
const TX_HZ: u32 = 880;
const TX_MS: u32 = 80;
const RX_HZ: u32 = 660;
const RX_MS: u32 = 140;

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

fn fill_beep(buf: &mut [u8], hz: u32) {
    let frames = buf.len() / 2;
    let mut phase: u32 = 0;
    let step = (hz.saturating_mul(65536)) / SAMPLE_RATE;
    for i in 0..frames {
        let env = ((frames - i) as u32 * 22_000) / frames as u32;
        let s: i16 = if (phase & 0x8000) != 0 {
            env as i16
        } else {
            -(env as i16)
        };
        let b = s.to_le_bytes();
        buf[i * 2] = b[0];
        buf[i * 2 + 1] = b[1];
        phase = phase.wrapping_add(step);
    }
}

fn tone_for(kind: AudioKind) -> (u32, u32) {
    match kind {
        AudioKind::Tx => (TX_HZ, TX_MS),
        AudioKind::Rx => (RX_HZ, RX_MS),
    }
}

pub struct AudioHw {
    i2c: I2c<'static, Blocking>,
    codec: Es8311,
    pa: PaGpio,
    ready: bool,
    tx: Option<I2sTx<'static, Blocking>>,
    dma: Option<DmaTxBuf>,
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
        dma_ch: DMA_CH0<'static>,
    ) -> Self {
        let out_cfg = OutputConfig::default();
        let i2c = I2c::new(i2c0, I2cConfig::default().with_frequency(Rate::from_khz(100)))
            .expect("i2c")
            .with_sda(sda)
            .with_scl(scl);
        let mut pa = PaGpio {
            pa_en: Output::new(pa_en, Level::High, out_cfg),
            pa_ctrl: Output::new(pa_ctrl, Level::Low, out_cfg),
        };
        audio_rail_enable(&mut pa, false);
        audio_amp_enable(&mut pa, false);

        let cfg = TdmConfig::new_tdm_philips()
            .with_sample_rate(Rate::from_hz(SAMPLE_RATE))
            .with_data_format(DataFormat::Data16Channel16)
            .with_channels(Channels::LEFT);
        let i2s = I2s::new(i2s0, dma_ch, cfg)
            .expect("i2s")
            .with_mclk(mclk);
        let tx = i2s
            .i2s_tx
            .with_bclk(sclk)
            .with_ws(lrck)
            .with_dout(dout)
            .build();

        let dma = dma_tx_buffer!(8192).expect("dma tx");

        Self {
            i2c,
            codec: Es8311::new(ES8311_ADDR),
            pa,
            ready: false,
            tx: Some(tx),
            dma: Some(dma),
        }
    }

    pub fn init_codec(&mut self) -> bool {
        let clock = ClockConfig {
            mclk_inverted: false,
            sclk_inverted: false,
            mclk_from_mclk_pin: true,
            mclk_frequency: MCLK_HZ,
            sample_frequency: SAMPLE_RATE,
        };
        let mut delay = Delay::new();
        let ok = self
            .codec
            .init(
                &mut self.i2c,
                &clock,
                Resolution::Bits16,
                Resolution::Bits16,
                &mut delay,
            )
            .is_ok()
            && self.codec.volume_set(&mut self.i2c, 70, None).is_ok();
        self.ready = ok;
        println!("AUDIO: ES8311 {}", if ok { "ok" } else { "fail" });
        ok
    }

    pub async fn beep(&mut self, kind: AudioKind) {
        if !self.ready && !self.init_codec() {
            return;
        }
        let (hz, ms) = tone_for(kind);
        let Some(tx) = self.tx.take() else {
            return;
        };
        let Some(mut dma) = self.dma.take() else {
            self.tx = Some(tx);
            return;
        };

        audio_rail_enable(&mut self.pa, true);
        Timer::after(Duration::from_millis(5)).await;
        audio_amp_enable(&mut self.pa, true);

        {
            let buf = dma.as_mut_slice();
            let use_len = ((SAMPLE_RATE * ms / 1000) as usize * 2).min(buf.len());
            fill_beep(&mut buf[..use_len], hz);
            if use_len < buf.len() {
                buf[use_len..].fill(0);
            }
            let _ = dma.set_length(use_len);
        }

        match tx.write(dma) {
            Ok(transfer) => {
                let (_res, tx_back, dma_back) = transfer.wait();
                self.tx = Some(tx_back);
                self.dma = Some(dma_back);
                println!("AUDIO: {:?} {hz}Hz/{ms}ms", kind);
            }
            Err((_e, tx_back, dma_back)) => {
                self.tx = Some(tx_back);
                self.dma = Some(dma_back);
                println!("AUDIO: I2S write fail");
            }
        }

        audio_amp_enable(&mut self.pa, false);
        audio_rail_enable(&mut self.pa, false);
    }
}
