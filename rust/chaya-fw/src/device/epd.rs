//! GDEM0154F51H (JD79660) splash / heart paint via epdsi.

use embedded_hal_bus::spi::ExclusiveDevice;
use epdsi::prelude::*;
use esp_hal::{
    delay::Delay,
    gpio::{Input, InputConfig, Level, Output, OutputConfig, Pull},
    peripherals::{GPIO8, GPIO9, GPIO10, GPIO11, GPIO12, GPIO13, SPI2},
    spi::master::{Config as SpiConfig, Spi},
    time::Rate,
};
use esp_println::println;
use static_cell::StaticCell;

const W: u32 = GDEM0154F51H::WIDTH;
const H: u32 = GDEM0154F51H::HEIGHT;
const FRAME_LEN: usize = (W as usize * H as usize) / 4; // 2 bpp

const BLACK: u8 = 0b00;
const WHITE: u8 = 0b01;
const RED: u8 = 0b11;

fn set_px(buf: &mut [u8], x: i32, y: i32, color: u8) {
    if x < 0 || y < 0 || x >= W as i32 || y >= H as i32 {
        return;
    }
    let i = (y as u32 * W + x as u32) as usize;
    let bi = i / 4;
    let shift = 6 - (i % 4) * 2;
    let mask = !(0b11u8 << shift);
    buf[bi] = (buf[bi] & mask) | ((color & 0b11) << shift);
}

fn fill(buf: &mut [u8], color: u8) {
    let b = match color & 0b11 {
        BLACK => 0x00,
        WHITE => 0x55,
        0b10 => 0xAA,
        _ => 0xFF,
    };
    buf.fill(b);
}

fn fill_rect(buf: &mut [u8], x0: i32, y0: i32, w: i32, h: i32, color: u8) {
    for y in y0..y0 + h {
        for x in x0..x0 + w {
            set_px(buf, x, y, color);
        }
    }
}

fn draw_heart(buf: &mut [u8], cx: i32, cy: i32, scale: i32, color: u8) {
    // Integer heart: two lobes + V body (no f32).
    let r = scale;
    for dy in -2 * r..3 * r {
        for dx in -2 * r..=2 * r {
            let in_left = (dx + r) * (dx + r) + (dy + r / 2) * (dy + r / 2) <= (r * r * 6) / 10;
            let in_right = (dx - r) * (dx - r) + (dy + r / 2) * (dy + r / 2) <= (r * r * 6) / 10;
            let in_bottom = dy >= 0
                && dy <= (r * 18) / 10
                && dx.abs() <= r + r / 2 - (dy * (r + r / 2)) / ((r * 18) / 10).max(1);
            if in_left || in_right || in_bottom {
                set_px(buf, cx + dx, cy + dy, color);
            }
        }
    }
}

fn paint_frame(frame: &mut [u8], mode: PaintMode) {
    fill(frame, WHITE);
    match mode {
        PaintMode::Splash => {
            fill_rect(frame, 20, 20, 160, 8, BLACK);
            fill_rect(frame, 20, 172, 160, 8, BLACK);
            draw_heart(frame, 100, 90, 28, RED);
            fill_rect(frame, 70, 150, 60, 10, BLACK);
        }
        PaintMode::Heart => {
            draw_heart(frame, 100, 100, 36, RED);
        }
    }
}

#[derive(Clone, Copy, Debug)]
pub enum PaintMode {
    Splash,
    Heart,
}

/// Blocking full refresh (~20s). Consumes SPI2 + EPD GPIOs.
pub fn paint(
    spi2: SPI2<'static>,
    sck: GPIO12<'static>,
    mosi: GPIO13<'static>,
    cs_pin: GPIO11<'static>,
    dc_pin: GPIO10<'static>,
    rst_pin: GPIO9<'static>,
    busy_pin: GPIO8<'static>,
    mode: PaintMode,
) -> bool {
    println!("EPD: init GDEM0154F51H ({mode:?})");

    let out_cfg = OutputConfig::default();
    let in_cfg = InputConfig::default().with_pull(Pull::Up);

    let spi = match Spi::new(spi2, SpiConfig::default().with_frequency(Rate::from_mhz(4))) {
        Ok(s) => s.with_sck(sck).with_mosi(mosi),
        Err(_) => {
            println!("EPD: SPI init failed");
            return false;
        }
    };

    let cs = Output::new(cs_pin, Level::High, out_cfg);
    let dc = Output::new(dc_pin, Level::Low, out_cfg);
    let rst = Output::new(rst_pin, Level::High, out_cfg);
    let busy = Input::new(busy_pin, in_cfg);

    let mut delay = Delay::new();
    let spi_dev = match ExclusiveDevice::new(spi, cs, Delay::new()) {
        Ok(d) => d,
        Err(_) => {
            println!("EPD: ExclusiveDevice failed");
            return false;
        }
    };

    let bus = SpiBusWrapper::new(spi_dev, dc, rst, busy);
    let controller = Jd79660Controller::new(W, H);
    let mut epd = EpdBuilder::<_, GDEM0154F51H>::new(controller).build(bus);

    if epd.init(&mut delay).is_err() {
        println!("EPD: init failed");
        return false;
    }

    static FRAME: StaticCell<[u8; FRAME_LEN]> = StaticCell::new();
    let frame = FRAME.init([0x55u8; FRAME_LEN]);
    paint_frame(frame, mode);

    if epd
        .display_frame(ColorChannel::BlackWhite, frame, &mut delay)
        .is_err()
    {
        println!("EPD: display_frame failed");
        let _ = epd.sleep(&mut delay);
        return false;
    }

    let _ = epd.sleep(&mut delay);
    println!("EPD: refresh done");
    true
}
