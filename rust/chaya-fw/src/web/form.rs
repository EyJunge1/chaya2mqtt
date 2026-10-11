//! SoftAP/STA admin HTTP helpers (status, captive probes, SSE stub).


/// First request line path (no query), best-effort.
pub fn request_path(req: &[u8]) -> &str {
    let Ok(s) = core::str::from_utf8(req) else {
        return "/";
    };
    let line = s.lines().next().unwrap_or("");
    let mut parts = line.split_whitespace();
    let _method = parts.next();
    let path = parts.next().unwrap_or("/");
    path.split('?').next().unwrap_or("/")
}

pub fn is_get(req: &[u8]) -> bool {
    req.starts_with(b"GET ")
}

pub fn is_post(req: &[u8]) -> bool {
    req.starts_with(b"POST ")
}

pub fn body(req: &[u8]) -> &[u8] {
    req.windows(4)
        .position(|w| w == b"\r\n\r\n")
        .map(|i| &req[i + 4..])
        .unwrap_or(&[])
}

pub fn form_value<'a>(body: &'a [u8], key: &str) -> Option<&'a str> {
    let Ok(s) = core::str::from_utf8(body) else {
        return None;
    };
    for pair in s.split('&') {
        let mut it = pair.splitn(2, '=');
        let k = it.next()?;
        let v = it.next().unwrap_or("");
        if k == key {
            return Some(percent_decode_simple(v));
        }
    }
    None
}

/// Best-effort `"key":"value"` extract from a small JSON body.
pub fn json_str_field<'a>(body: &'a [u8], key: &str) -> Option<&'a str> {
    let Ok(s) = core::str::from_utf8(body) else {
        return None;
    };
    let mut needle = heapless::String::<48>::new();
    let _ = needle.push('"');
    let _ = needle.push_str(key);
    let _ = needle.push_str("\"");
    let i = s.find(needle.as_str())?;
    let rest = &s[i + needle.len()..];
    let colon = rest.find(':')?;
    let after = rest[colon + 1..].trim_start();
    let after = after.strip_prefix('"')?;
    let end = after.find('"')?;
    Some(&after[..end])
}

/// Best-effort `"key":123` / `"key":true` extract.
pub fn json_num_field(body: &[u8], key: &str) -> Option<i32> {
    let Ok(s) = core::str::from_utf8(body) else {
        return None;
    };
    let mut needle = heapless::String::<48>::new();
    let _ = needle.push('"');
    let _ = needle.push_str(key);
    let _ = needle.push('"');
    let i = s.find(needle.as_str())?;
    let rest = &s[i + needle.len()..];
    let colon = rest.find(':')?;
    let after = rest[colon + 1..].trim_start();
    let end = after
        .find(|c: char| !(c.is_ascii_digit() || c == '-' || c == '+'))
        .unwrap_or(after.len());
    after[..end].parse().ok()
}

pub fn json_bool_field(body: &[u8], key: &str) -> Option<bool> {
    let Ok(s) = core::str::from_utf8(body) else {
        return None;
    };
    let mut needle = heapless::String::<48>::new();
    let _ = needle.push('"');
    let _ = needle.push_str(key);
    let _ = needle.push('"');
    let i = s.find(needle.as_str())?;
    let rest = &s[i + needle.len()..];
    let colon = rest.find(':')?;
    let after = rest[colon + 1..].trim_start();
    if after.starts_with("true") {
        Some(true)
    } else if after.starts_with("false") {
        Some(false)
    } else {
        None
    }
}

fn percent_decode_simple(s: &str) -> &str {
    // keep raw for ASCII ssids/pass; '+' → space is rare for wifi
    s
}

