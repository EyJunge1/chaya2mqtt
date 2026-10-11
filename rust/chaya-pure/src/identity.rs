//! From `src/identity/device_identity_pure.h`.

use crate::constants::{device_id_syntax_ok, DEVICE_ID_BUF_LEN, DEVICE_ID_HEX_LEN};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DeviceIdFormatError {
    BadArgs,
    FormatFailed,
}

/// Format 3 bytes as 6 lowercase hex chars into `out` (needs ≥ `DEVICE_ID_BUF_LEN`).
pub fn device_id_format_from_bytes(
    bytes: &[u8; 3],
    out: &mut [u8],
) -> Result<(), DeviceIdFormatError> {
    if out.len() < DEVICE_ID_BUF_LEN {
        if !out.is_empty() {
            out[0] = 0;
        }
        return Err(DeviceIdFormatError::BadArgs);
    }
    const HEX: &[u8; 16] = b"0123456789abcdef";
    for (i, &b) in bytes.iter().enumerate() {
        out[i * 2] = HEX[(b >> 4) as usize];
        out[i * 2 + 1] = HEX[(b & 0x0f) as usize];
    }
    out[DEVICE_ID_HEX_LEN] = 0;
    let s = core::str::from_utf8(&out[..DEVICE_ID_HEX_LEN]).map_err(|_| DeviceIdFormatError::FormatFailed)?;
    if !device_id_syntax_ok(s) {
        out[0] = 0;
        return Err(DeviceIdFormatError::FormatFailed);
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn formats_hex() {
        let mut buf = [0u8; DEVICE_ID_BUF_LEN];
        device_id_format_from_bytes(&[0xa1, 0xb2, 0xc3], &mut buf).unwrap();
        assert_eq!(&buf[..6], b"a1b2c3");
        assert_eq!(buf[6], 0);
    }
}
