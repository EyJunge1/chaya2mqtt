//! Button + LED task.

use chaya_fw::board::pins;
use chaya_fw::input::LedPreset;
use chaya_fw::storage::platform::{GpioIn, GpioOut};
use chaya_pure::audio::AudioKind;
use embassy_time::{Duration, Timer};
use esp_hal::gpio::{Input, Level, Output};
use esp_println::println;

use crate::runtime::state::*;
use crate::runtime::util::MonoClock;

struct BtnGpio {
    boot: Input<'static>,
    pwr: Input<'static>,
}

impl GpioIn for BtnGpio {
    fn get_input(&mut self, pin: u8) -> bool {
        match pin {
            p if p == pins::BUTTON => self.boot.is_high(),
            p if p == pins::PWR_BUTTON => self.pwr.is_high(),
            _ => true,
        }
    }
}

struct LedGpio {
    led: Output<'static>,
}

impl GpioOut for LedGpio {
    fn set_output(&mut self, pin: u8, high: bool) {
        if pin == pins::BUTTON_LED {
            self.led.set_level(if high { Level::High } else { Level::Low });
        }
    }
}

#[embassy_executor::task]
pub async fn button_led_task(
    boot: Input<'static>,
    pwr: Input<'static>,
    led: Output<'static>,
    mut bat_latch: Output<'static>,
) {
    let mut btns = BtnGpio { boot, pwr };
    let mut led_gpio = LedGpio { led };
    let mut button = chaya_fw::input::ButtonService::default();
    let mut led_svc = chaya_fw::input::LedService::default();
    led_svc.startup_blink();
    let clock = MonoClock;

    loop {
        let _ = led_svc.tick_phase_done(&mut led_gpio);
        if button.poll_boot_press(&mut btns, &clock) {
            let next = {
                let mut t = HEART_TX.lock().await;
                *t = t.saturating_add(1);
                *t
            };
            HEART_BUMP.signal(next);
            AUDIO_BEEP.signal(AudioKind::Tx);
            HEART_DIRTY.signal(());
            led_svc.play_preset(LedPreset::TxAck);
            println!("BTN: heart bump → {next}");
        }
        if let Some(p) = LED_PRESET.try_take() {
            led_svc.play_preset(p);
        }
        if MQTT_UP.try_take().is_some() {
            led_svc.play_preset(LedPreset::MqttUp);
        }
        if button.soft_off_ready(&mut btns, &clock) {
            println!("PWR: soft-off — cutting battery latch");
            led_svc.play_preset(LedPreset::SoftOff);
            for _ in 0..10 {
                let _ = led_svc.tick_phase_done(&mut led_gpio);
                Timer::after(Duration::from_millis(40)).await;
            }
            bat_latch.set_level(Level::Low);
            loop {
                Timer::after(Duration::from_secs(60)).await;
            }
        }
        Timer::after(Duration::from_millis(20)).await;
    }
}
