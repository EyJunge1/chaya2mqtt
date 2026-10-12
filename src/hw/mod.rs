//! Waveshare ESP32-S3-ePaper-1.54G (SKU 34586) pin map — from `pins_esp32_waveshare.h`.

pub mod pins {
    pub const BUTTON: u8 = 0;
    pub const BUTTON_LED: u8 = 3;

    pub const DISPLAY_PWR_EN: u8 = 6;
    pub const DISPLAY_BUSY: u8 = 8;
    pub const DISPLAY_RST: u8 = 9;
    pub const DISPLAY_DC: u8 = 10;

    pub const SPI_MOSI: u8 = 13;
    pub const SPI_SCK: u8 = 12;
    pub const SPI_CS: u8 = 11;
    /// No EPD MISO on this board.
    pub const SPI_MISO: Option<u8> = None;

    pub const BAT_CONTROL: u8 = 17;
    pub const BAT_ADC: u8 = 4;
    pub const PWR_BUTTON: u8 = 18;

    pub const I2C_SDA: u8 = 47;
    pub const I2C_SCL: u8 = 48;

    pub const I2S_MCLK: u8 = 14;
    pub const I2S_SCLK: u8 = 15;
    pub const I2S_LRCK: u8 = 38;
    pub const I2S_DSDIN: u8 = 45;
    pub const PA_EN: u8 = 42;
    pub const PA_CTRL: u8 = 46;

    pub const SD_CLK: u8 = 39;
    pub const SD_MISO: u8 = 40;
    pub const SD_MOSI: u8 = 41;
}

use crate::util::platform::GpioOut;

/// Battery latch HIGH before PWR release, else LiPo cuts.
pub fn battery_latch_on(gpio: &mut dyn GpioOut) {
    gpio.set_output(pins::BAT_CONTROL, true);
}

/// Hold microSD CLK/DAT0/CMD LOW (no mount) so lines do not float-draw.
pub fn sd_hold_off(gpio: &mut dyn GpioOut) {
    gpio.set_output(pins::SD_CLK, false);
    gpio.set_output(pins::SD_MISO, false);
    gpio.set_output(pins::SD_MOSI, false);
}

/// EPD3V3_EN: drive LOW to power the panel.
pub fn display_power_enable(gpio: &mut dyn GpioOut, on: bool) {
    gpio.set_output(pins::DISPLAY_PWR_EN, !on);
}

/// Audio rail: PA_EN active-low; PA_CTRL high while playing.
pub fn audio_rail_enable(gpio: &mut dyn GpioOut, on: bool) {
    gpio.set_output(pins::PA_EN, !on);
}

pub fn audio_amp_enable(gpio: &mut dyn GpioOut, on: bool) {
    gpio.set_output(pins::PA_CTRL, on);
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::util::platform::SimGpio;

    #[test]
    fn latch_and_sd_hold() {
        let mut g = SimGpio::default();
        battery_latch_on(&mut g);
        assert_eq!(g.get(pins::BAT_CONTROL), Some(true));
        sd_hold_off(&mut g);
        assert_eq!(g.get(pins::SD_CLK), Some(false));
        display_power_enable(&mut g, true);
        assert_eq!(g.get(pins::DISPLAY_PWR_EN), Some(false));
    }
}
