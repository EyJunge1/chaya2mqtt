//! Shared constants used by pure modules (from `constants.h` / config headers).

pub const DEVICE_ID_HEX_LEN: usize = 6;
pub const DEVICE_ID_BUF_LEN: usize = 7;
pub const DISPLAY_COUNTER_MAX: i32 = 999;
/// Continuous Wi-Fi/MQTT outage before heart switches to crack (C++ `kDisplayOfflineGraceMs`).
pub const DISPLAY_OFFLINE_GRACE_MS: u32 = 300_000;
/// Minimum plausible UTC after NTP sync — rejects unset RTC (~1970).
pub const NTP_MIN_VALID_UTC_EPOCH: u64 = 1_700_000_000;
/// Sentinel for "no reset-day anchor yet" (C++ `UINT32_MAX`).
pub const HEART_LAST_RESET_DAY_UNSET: u32 = u32::MAX;
pub const AUDIO_HOUR_MAX: u8 = 23;
pub const BATTERY_CRITICAL_PCT: i32 = 10;
pub const WIFI_SSID_MAX_LEN: usize = 33;
pub const WIFI_PASS_MAX_LEN: usize = 65;
pub const IPV4_STR_MAX_LEN: usize = 16;
pub const NVS_NS_CHAYA: &str = "chaya";

pub const SSE_CHAYA: u32 = 1 << 0;
pub const SSE_WIFI: u32 = 1 << 1;
pub const SSE_MQTT: u32 = 1 << 2;
pub const SSE_OTA: u32 = 1 << 3;
pub const SSE_DEVICE: u32 = 1 << 4;
pub const SSE_ALL: u32 = 0x1f;

/// Six lowercase hex digits (a-f0-9), e.g. `a1b2c3`.
pub fn device_id_syntax_ok(id: &str) -> bool {
    let b = id.as_bytes();
    if b.len() != DEVICE_ID_HEX_LEN {
        return false;
    }
    b.iter().all(|&c| matches!(c, b'0'..=b'9' | b'a'..=b'f'))
}
