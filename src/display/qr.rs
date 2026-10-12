//! Encode WIFI QR payload and paint onto a 2bpp EPD framebuffer helper.

use qrcodegen_no_heap::{QrCode, QrCodeEcc, Version};

/// Encode text into out/temp buffers; returns QR size (modules) or None.
pub fn encode_text<'a>(
    text: &str,
    temp: &'a mut [u8],
    out: &'a mut [u8],
) -> Option<QrCode<'a>> {
    let max = Version::new(8);
    let need = max.buffer_len();
    if temp.len() < need || out.len() < need {
        return None;
    }
    QrCode::encode_text(
        text,
        &mut temp[..need],
        &mut out[..need],
        QrCodeEcc::Medium,
        Version::MIN,
        max,
        None,
        true,
    )
    .ok()
}

/// Draw QR modules as black squares onto a callback `set_px(x,y)`.
pub fn paint_qr<F: FnMut(i32, i32)>(
    qr: &QrCode<'_>,
    origin_x: i32,
    origin_y: i32,
    module_px: i32,
    mut set_black: F,
) {
    let n = qr.size();
    for y in 0..n {
        for x in 0..n {
            if qr.get_module(x, y) {
                let px = origin_x + x * module_px;
                let py = origin_y + y * module_px;
                for dy in 0..module_px {
                    for dx in 0..module_px {
                        set_black(px + dx, py + dy);
                    }
                }
            }
        }
    }
}
