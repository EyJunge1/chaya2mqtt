//! GDEM0154F51H (JD79660) — splash / heart with Lucide icons + counters.

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

use super::icons;
use crate::core::display::{display_battery_color, display_battery_icon, DisplayBatteryColor, DisplayBatteryIcon};

// GDEM0154F51H visible geometry (epdsi panel trait WIDTH/HEIGHT).
const W: u32 = 200;
const H: u32 = 200;
const FRAME_LEN: usize = (W as usize * H as usize) / 4; // 2 bpp

const BLACK: u8 = 0b00;
const WHITE: u8 = 0b01;
const YELLOW: u8 = 0b10;
const RED: u8 = 0b11;

const RIGHT_MARGIN: i32 = 4;
const LEFT_MARGIN: i32 = 4;
const BATTERY_TOP_MARGIN: i32 = 4;
const FOOTER_TEXT_TOP: i32 = 167;
const HEART_FOOTER_GAP: i32 = 4;
const ARROW_LANE: i32 = icons::MOVE_DOWN_W as i32 + 5;

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
        YELLOW => 0xAA,
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

/// Adafruit-GFX MSB 1-bit blit (row-packed, MSB = leftmost pixel).
fn blit_msb(buf: &mut [u8], x0: i32, y0: i32, data: &[u8], w: i16, h: i16, color: u8) {
    let w = w as i32;
    let h = h as i32;
    let row_bytes = ((w + 7) / 8) as usize;
    for row in 0..h {
        let row_off = row as usize * row_bytes;
        for col in 0..w {
            let byte = data.get(row_off + (col as usize >> 3)).copied().unwrap_or(0);
            if byte & (0x80 >> (col & 7)) != 0 {
                set_px(buf, x0 + col, y0 + row, color);
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
        b'+' => [0b000, 0b010, 0b111, 0b010, 0b000],
        b'A' => [0b010, 0b101, 0b111, 0b101, 0b101],
        b'C' => [0b111, 0b100, 0b100, 0b100, 0b111],
        b'H' => [0b101, 0b101, 0b111, 0b101, 0b101],
        b'M' => [0b101, 0b111, 0b111, 0b101, 0b101],
        b'P' => [0b111, 0b101, 0b111, 0b100, 0b100],
        b'Q' => [0b111, 0b101, 0b101, 0b111, 0b001],
        b'T' => [0b111, 0b010, 0b010, 0b010, 0b010],
        b'U' => [0b101, 0b101, 0b101, 0b101, 0b111],
        b'X' => [0b101, 0b101, 0b010, 0b101, 0b101],
        b'R' => [0b111, 0b101, 0b111, 0b110, 0b101],
        b'Y' => [0b101, 0b101, 0b010, 0b010, 0b010],
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

fn text_width(s: &str, scale: i32) -> i32 {
    (s.len() as i32) * 4 * scale
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

fn battery_color(pct: i32) -> u8 {
    match display_battery_color(pct) {
        DisplayBatteryColor::Red => RED,
        DisplayBatteryColor::Yellow => YELLOW,
        DisplayBatteryColor::Black => BLACK,
    }
}

fn draw_battery_lucide(buf: &mut [u8], x: i32, y: i32, pct: i32) {
    let (data, w, h) = match display_battery_icon(pct) {
        DisplayBatteryIcon::Full => (icons::BATTERY_FULL, icons::BATTERY_FULL_W, icons::BATTERY_FULL_H),
        DisplayBatteryIcon::Medium => {
            (icons::BATTERY_MEDIUM, icons::BATTERY_MEDIUM_W, icons::BATTERY_MEDIUM_H)
        }
        DisplayBatteryIcon::Low => (icons::BATTERY_LOW, icons::BATTERY_LOW_W, icons::BATTERY_LOW_H),
        DisplayBatteryIcon::Empty => {
            (icons::BATTERY_EMPTY, icons::BATTERY_EMPTY_W, icons::BATTERY_EMPTY_H)
        }
    };
    blit_msb(buf, x, y, data, w, h, battery_color(pct));
}

fn draw_battery_top_right(buf: &mut [u8], pct: i32) {
    let x = W as i32 - RIGHT_MARGIN - icons::BATTERY_FULL_W as i32;
    draw_battery_lucide(buf, x, BATTERY_TOP_MARGIN, pct);
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
    /// Soft-off splash (Lucide heart-off).
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
            // fallback without QR — Lucide heart for parity
            fill_rect(frame, 16, 8, 168, 4, BLACK);
            draw_text(frame, 28, 16, "AP", 2, BLACK);
            let hx = (W as i32 - icons::HEART_W as i32) / 2;
            let hy = 30;
            blit_msb(
                frame,
                hx,
                hy,
                icons::HEART,
                icons::HEART_W,
                icons::HEART_H,
                RED,
            );
            draw_text(frame, 16, 176, ssid.as_str(), 2, BLACK);
            fill_rect(frame, 16, 192, 168, 4, BLACK);
        }
        Scene::Heart {
            tx,
            rx,
            bat_pct,
            cracked,
        } => {
            let show_footer = !*cracked;
            let (bmp, bw, bh) = if *cracked {
                (icons::HEART_CRACK, icons::HEART_CRACK_W, icons::HEART_CRACK_H)
            } else {
                (icons::HEART, icons::HEART_W, icons::HEART_H)
            };
            let heart_x = (W as i32 - bw as i32) / 2;
            let heart_y = if show_footer {
                FOOTER_TEXT_TOP - bh as i32 - HEART_FOOTER_GAP
            } else {
                (H as i32 - bh as i32) / 2
            };
            // C++ draws both filled and crack in red.
            blit_msb(frame, heart_x, heart_y, bmp, bw, bh, RED);

            if show_footer {
                let down_x = LEFT_MARGIN;
                let up_x = W as i32 - RIGHT_MARGIN - icons::MOVE_UP_W as i32;
                blit_msb(
                    frame,
                    down_x,
                    FOOTER_TEXT_TOP,
                    icons::MOVE_DOWN,
                    icons::MOVE_DOWN_W,
                    icons::MOVE_DOWN_H,
                    BLACK,
                );
                blit_msb(
                    frame,
                    up_x,
                    FOOTER_TEXT_TOP,
                    icons::MOVE_UP,
                    icons::MOVE_UP_W,
                    icons::MOVE_UP_H,
                    BLACK,
                );

                let mut num = heapless::String::<12>::new();
                itoa_buf(*rx, &mut num);
                let rx_scale = if num.len() <= 3 { 3 } else { 2 };
                draw_text(
                    frame,
                    LEFT_MARGIN + ARROW_LANE,
                    FOOTER_TEXT_TOP,
                    num.as_str(),
                    rx_scale,
                    BLACK,
                );

                itoa_buf(*tx, &mut num);
                let tx_scale = if num.len() <= 3 { 3 } else { 2 };
                let tw = text_width(num.as_str(), tx_scale);
                let tx_x = W as i32 - RIGHT_MARGIN - ARROW_LANE - tw;
                draw_text(frame, tx_x, FOOTER_TEXT_TOP, num.as_str(), tx_scale, BLACK);
            }

            draw_battery_top_right(frame, *bat_pct);
        }
        Scene::PowerOff => {
            // Title + Lucide heart-off (matches C++ drawPowerOffScreen layout intent).
            const TITLE: &str = "CHAYA2MQTT";
            const TITLE_SCALE: i32 = 2;
            const GAP: i32 = 10;
            const HEART_OFF_TOP_INSET: i32 = 7;
            let title_w = text_width(TITLE, TITLE_SCALE);
            let title_x = (W as i32 - title_w) / 2;
            let title_y = GAP;
            draw_text(frame, title_x, title_y, TITLE, TITLE_SCALE, RED);
            let title_h = 5 * TITLE_SCALE;
            let heart_x = (W as i32 - icons::HEART_OFF_W as i32) / 2;
            let heart_y = title_y + title_h + GAP - HEART_OFF_TOP_INSET;
            blit_msb(
                frame,
                heart_x,
                heart_y,
                icons::HEART_OFF,
                icons::HEART_OFF_W,
                icons::HEART_OFF_H,
                BLACK,
            );
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
