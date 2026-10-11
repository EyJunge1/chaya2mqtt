//! GDEM0154F51H (JD79660) — splash / heart with counters + battery bars.

use embedded_hal_bus::spi::ExclusiveDevice;
use epdsi::prelude::*;
use esp_hal::{
    delay::Delay,
    gpio::{Input, InputConfig, Level, Output, OutputConfig, Pull},
    peripherals::{GPIO8, GPIO9, GPIO10, GPIO11, GPIO12, GPIO13, SPI2},
    spi::master::{Config as SpiConfig, Spi},
    time::Rate,
    Blocking,
};
use esp_println::println;
use static_cell::StaticCell;

// GDEM0154F51H visible geometry (epdsi panel trait WIDTH/HEIGHT).
const W: u32 = 200;
const H: u32 = 200;
const FRAME_LEN: usize = (W as usize * H as usize) / 4; // 2 bpp

const BLACK: u8 = 0b00;
const WHITE: u8 = 0b01;
const RED: u8 = 0b11;

type SpiDev = ExclusiveDevice<Spi<'static, Blocking>, Output<'static>, Delay>;
type Bus = SpiBusWrapper<SpiDev, Output<'static>, Output<'static>, Input<'static>>;
type Driver = EpdDriver<Bus, Jd79660Controller, GDEM0154F51H>;

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

fn glyph3x5(c: u8) -> [u8; 5] {
    match c {
        b'0' => [0b111, 0b101, 0b101, 0b101, 0b111],
        b'1' => [0b010, 0b110, 0b010, 0b010, 0b111],
        b'2' => [0b111, 0b001, 0b111, 0b100, 0b111],
        b'3' => [0b111, 0b001, 0b111, 0b001, 0b111],
        b'4' => [0b101, 0b101, 0b111, 0b001, 0b001],
        b'5' => [0b111, 0b100, 0b111, 0b001, 0b111],
        b'6' => [0b111, 0b100, 0b111, 0b101, 0b111],
        b'7' => [0b111, 0b001, 0b010, 0b010, 0b010],
        b'8' => [0b111, 0b101, 0b111, 0b101, 0b111],
        b'9' => [0b111, 0b101, 0b111, 0b001, 0b111],
        b'-' => [0b000, 0b000, 0b111, 0b000, 0b000],
        b'A' => [0b010, 0b101, 0b111, 0b101, 0b101],
        b'P' => [0b111, 0b101, 0b111, 0b100, 0b100],
        b'T' => [0b111, 0b010, 0b010, 0b010, 0b010],
        b'X' => [0b101, 0b101, 0b010, 0b101, 0b101],
        b'R' => [0b111, 0b101, 0b111, 0b110, 0b101],
        b'.' => [0b000, 0b000, 0b000, 0b000, 0b010],
        _ => [0b000, 0b000, 0b000, 0b000, 0b000],
    }
}

fn draw_char(buf: &mut [u8], x: i32, y: i32, c: u8, scale: i32, color: u8) {
    let g = glyph3x5(c.to_ascii_uppercase());
    for (row, bits) in g.iter().enumerate() {
        for col in 0..3 {
            if (bits >> (2 - col)) & 1 != 0 {
                fill_rect(
                    buf,
                    x + col * scale,
                    y + row as i32 * scale,
                    scale,
                    scale,
                    color,
                );
            }
        }
    }
}

fn draw_text(buf: &mut [u8], mut x: i32, y: i32, s: &str, scale: i32, color: u8) {
    for b in s.bytes() {
        draw_char(buf, x, y, b, scale, color);
        x += 4 * scale;
    }
}

fn itoa_buf(n: i32, out: &mut heapless::String<12>) {
    out.clear();
    let mut n = n;
    if n < 0 {
        let _ = out.push('-');
        n = -n;
    }
    if n == 0 {
        let _ = out.push('0');
        return;
    }
    let mut digs = heapless::Vec::<u8, 12>::new();
    while n > 0 {
        let _ = digs.push(b'0' + (n % 10) as u8);
        n /= 10;
    }
    for d in digs.iter().rev() {
        let _ = out.push(*d as char);
    }
}

fn draw_battery(buf: &mut [u8], x: i32, y: i32, pct: i32) {
    fill_rect(buf, x, y, 24, 12, BLACK);
    fill_rect(buf, x + 1, y + 1, 22, 10, WHITE);
    fill_rect(buf, x + 24, y + 3, 3, 6, BLACK);
    let p = pct.clamp(0, 100);
    let fill_w = (20 * p) / 100;
    if fill_w > 0 {
        let color = if p <= 15 { RED } else { BLACK };
        fill_rect(buf, x + 2, y + 2, fill_w, 8, color);
    }
}

#[derive(Clone, Debug)]
pub enum Scene {
    SoftAp {
        ssid: heapless::String<32>,
        psk: heapless::String<32>,
    },
    Heart {
        tx: i32,
        rx: i32,
        bat_pct: i32,
        cracked: bool,
    },
    /// Soft-off splash (heart with power-off glyph).
    PowerOff,
}

fn paint_scene(frame: &mut [u8], scene: &Scene) {
    fill(frame, WHITE);
    match scene {
        Scene::SoftAp { ssid, psk } => {
            // WIFI QR (MeCard) + labels
            let mut payload = [0u8; 128];
            let ok = crate::wifi::WlanService::build_setup_qr(ssid.as_str(), psk.as_str(), &mut payload);
            if ok {
                if let Ok(text) = core::str::from_utf8(
                    &payload[..payload.iter().position(|&c| c == 0).unwrap_or(payload.len())],
                ) {
                    let mut temp = [0u8; 320];
                    let mut out = [0u8; 320];
                    if let Some(qr) = crate::display::qr::encode_text(text, &mut temp, &mut out) {
                        let size = qr.size();
                        // fit in ~160px with quiet zone
                        let module = (160 / size).max(2);
                        let qr_px = size * module;
                        let ox = (W as i32 - qr_px) / 2;
                        let oy = 8;
                        fill_rect(frame, ox - 2, oy - 2, qr_px + 4, qr_px + 4, WHITE);
                        crate::display::qr::paint_qr(&qr, ox, oy, module, |x, y| {
                            set_px(frame, x, y, BLACK);
                        });
                        draw_text(frame, 8, 176, ssid.as_str(), 2, BLACK);
                        // show abbreviated PSK hint (first 4…)
                        if psk.len() >= 4 {
                            let mut hint = heapless::String::<16>::new();
                            let _ = hint.push_str(&psk.as_str()[..4]);
                            let _ = hint.push_str("..");
                            draw_text(frame, 8, 188, hint.as_str(), 2, BLACK);
                        }
                        return;
                    }
                }
            }
            // fallback without QR
            fill_rect(frame, 16, 12, 168, 6, BLACK);
            draw_text(frame, 28, 28, "AP", 3, BLACK);
            draw_heart(frame, 100, 88, 22, RED);
            draw_text(frame, 16, 140, ssid.as_str(), 2, BLACK);
            draw_text(frame, 16, 168, "4.3.2.1", 2, BLACK);
            fill_rect(frame, 16, 188, 168, 6, BLACK);
        }
        Scene::Heart {
            tx,
            rx,
            bat_pct,
            cracked,
        } => {
            draw_battery(frame, 168, 8, *bat_pct);
            if *cracked {
                draw_heart(frame, 100, 78, 30, BLACK);
                fill_rect(frame, 88, 70, 24, 20, WHITE);
            } else {
                draw_heart(frame, 100, 78, 30, RED);
            }
            let mut num = heapless::String::<12>::new();
            draw_text(frame, 16, 150, "TX", 2, BLACK);
            itoa_buf(*tx, &mut num);
            draw_text(frame, 56, 150, num.as_str(), 2, BLACK);
            draw_text(frame, 16, 172, "RX", 2, BLACK);
            itoa_buf(*rx, &mut num);
            draw_text(frame, 56, 172, num.as_str(), 2, BLACK);
        }
        Scene::PowerOff => {
            draw_heart(frame, 100, 88, 28, BLACK);
            fill_rect(frame, 96, 60, 8, 24, WHITE);
            fill_rect(frame, 92, 56, 16, 8, BLACK);
            draw_text(frame, 52, 160, "OFF", 3, BLACK);
        }
    }
}

pub struct EpdPanel {
    epd: Driver,
    frame: &'static mut [u8; FRAME_LEN],
}

impl EpdPanel {
    pub fn new(
        spi2: SPI2<'static>,
        sck: GPIO12<'static>,
        mosi: GPIO13<'static>,
        cs_pin: GPIO11<'static>,
        dc_pin: GPIO10<'static>,
        rst_pin: GPIO9<'static>,
        busy_pin: GPIO8<'static>,
    ) -> Option<Self> {
        let out_cfg = OutputConfig::default();
        let in_cfg = InputConfig::default().with_pull(Pull::Up);
        let spi = match Spi::new(spi2, SpiConfig::default().with_frequency(Rate::from_mhz(4))) {
            Ok(s) => s.with_sck(sck).with_mosi(mosi),
            Err(_) => {
                println!("EPD: SPI init failed");
                return None;
            }
        };
        let cs = Output::new(cs_pin, Level::High, out_cfg);
        let dc = Output::new(dc_pin, Level::Low, out_cfg);
        let rst = Output::new(rst_pin, Level::High, out_cfg);
        let busy = Input::new(busy_pin, in_cfg);
        let spi_dev = match ExclusiveDevice::new(spi, cs, Delay::new()) {
            Ok(d) => d,
            Err(_) => {
                println!("EPD: ExclusiveDevice failed");
                return None;
            }
        };
        let bus = SpiBusWrapper::new(spi_dev, dc, rst, busy);
        let controller = Jd79660Controller::new(W, H);
        let epd = EpdBuilder::<_, GDEM0154F51H>::new(controller).build(bus);
        static FRAME: StaticCell<[u8; FRAME_LEN]> = StaticCell::new();
        let frame = FRAME.init([0x55u8; FRAME_LEN]);
        Some(Self { epd, frame })
    }

    pub fn show(&mut self, scene: &Scene) -> bool {
        println!("EPD: paint");
        let mut delay = Delay::new();
        if self.epd.init(&mut delay).is_err() {
            println!("EPD: init failed");
            return false;
        }
        paint_scene(self.frame, scene);
        let ok = self
            .epd
            .display_frame(ColorChannel::BlackWhite, self.frame, &mut delay)
            .is_ok();
        let _ = self.epd.sleep(&mut delay);
        if ok {
            println!("EPD: refresh done");
        } else {
            println!("EPD: display_frame failed");
        }
        ok
    }

    /// Paint power-off splash and put the panel to sleep before deep sleep.
    pub fn power_off(&mut self) -> bool {
        self.show(&Scene::PowerOff)
    }
}
