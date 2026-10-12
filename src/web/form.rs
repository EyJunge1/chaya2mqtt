//! SoftAP/STA admin form body helpers (`application/x-www-form-urlencoded`).

use alloc::string::String;
use alloc::vec::Vec;

/// Value for `key` in a urlencoded body, with `%XX` / `+` decoding.
pub fn form_value(body: &[u8], key: &str) -> Option<String> {
    let Ok(s) = core::str::from_utf8(body) else {
        return None;
    };
    for pair in s.split('&') {
        let mut it = pair.splitn(2, '=');
        let k = it.next()?;
        let v = it.next().unwrap_or("");
        if k == key {
            return Some(percent_decode(v));
        }
    }
    None
}

fn percent_decode(s: &str) -> String {
    let bytes = s.as_bytes();
    let mut out = Vec::with_capacity(bytes.len());
    let mut i = 0;
    while i < bytes.len() {
        match bytes[i] {
            b'+' => {
                out.push(b' ');
                i += 1;
            }
            b'%' if i + 2 < bytes.len() => {
                match (hex_nibble(bytes[i + 1]), hex_nibble(bytes[i + 2])) {
                    (Some(h), Some(l)) => {
                        out.push((h << 4) | l);
                        i += 3;
                    }
                    _ => {
                        out.push(b'%');
                        i += 1;
                    }
                }
            }
            b => {
                out.push(b);
                i += 1;
            }
        }
    }
    String::from_utf8(out).unwrap_or_default()
}

fn hex_nibble(b: u8) -> Option<u8> {
    match b {
        b'0'..=b'9' => Some(b - b'0'),
        b'a'..=b'f' => Some(b - b'a' + 10),
        b'A'..=b'F' => Some(b - b'A' + 10),
        _ => None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn form_value_decodes_percent_and_plus() {
        let body = b"ssid=My%20Net&pass=a%2Bb+c";
        assert_eq!(form_value(body, "ssid").as_deref(), Some("My Net"));
        assert_eq!(form_value(body, "pass").as_deref(), Some("a+b c"));
        assert_eq!(form_value(body, "missing"), None);
    }

    #[test]
    fn percent_decode_keeps_utf8() {
        // "ü" = %C3%BC
        assert_eq!(percent_decode("caf%C3%A9"), "café");
    }
}
