//! Button + LED task.

use chaya_fw::core::async_shutdown::soft_off_allowed;
use chaya_fw::core::audio::AudioKind;
use chaya_fw::hw::pins;
use chaya_fw::led::LedPreset;
use chaya_fw::util::platform::{GpioIn, GpioOut};
use embassy_time::{Duration, Timer};
use esp_hal::gpio::{Input, Level, Output};
use esp_println::println;

use crate::async_rt::state::*;
use crate::async_rt::util::MonoClock;

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
            self.led
                .set_level(if high { Level::High } else { Level::Low });
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
    let mut button = chaya_fw::button::ButtonService::default();
    let mut led_svc = chaya_fw::led::LedService::default();
    let led_on = *LED_ENABLED.lock().await;
    if led_on {
        led_svc.startup_blink();
    } else {
        led_svc.force_off(&mut led_gpio);
    }
    let clock = MonoClock;

    loop {
        let led_on = *LED_ENABLED.lock().await;
        if !led_on && led_svc.active {
            led_svc.force_off(&mut led_gpio);
        }
        let _ = led_svc.tick_phase_done(&mut led_gpio);
        if button.poll_boot_press(&mut btns, &clock) {
            // Request only — HEART_TX / HEART_DIRTY apply after MQTT PUBACK (C++ parity).
            // LED: TxPending now; TxAck/TxFail come from session on PUBACK/timeout.
            HEART_BUMP.signal(1);
            AUDIO_BEEP.signal(AudioKind::Tx);
            LED_PRESET.signal(LedPreset::TxPending);
            println!("BTN: heart bump requested");
        }
        if let Some(p) = LED_PRESET.try_take() {
            led_svc.play_preset_gated(p, led_on, &mut led_gpio);
        }
        if MQTT_UP.try_take().is_some() {
            led_svc.play_preset_gated(LedPreset::MqttUp, led_on, &mut led_gpio);
        }
        if button.soft_off_ready(&mut btns, &clock) {
            let ota_busy = {
                let st = OTA_STATUS.lock().await;
                st.installing || st.checking
            };
            if !soft_off_allowed(ota_busy, false, false, false, false, false) {
                println!("PWR: soft-off deferred (OTA busy)");
            } else {
                println!("PWR: soft-off — EPD power-off + deep sleep");
                led_svc.play_preset_gated(LedPreset::SoftOff, led_on, &mut led_gpio);
                for _ in 0..10 {
                    let _ = led_svc.tick_phase_done(&mut led_gpio);
                    Timer::after(Duration::from_millis(40)).await;
                }
                EPD_POWER_OFF.signal(());
                Timer::after(Duration::from_millis(800)).await;
                chaya_fw::battery::sleep::power_off_deep_sleep(&mut btns.pwr, &mut bat_latch);
            }
        }
        Timer::after(Duration::from_millis(20)).await;
    }
}
