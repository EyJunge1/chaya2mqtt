//! Shared HTTP response-header helpers for OTA (`httparse`).

/// Parse HTTP status code from a response head (status line + headers).
pub fn parse_status(headers: &str) -> u16 {
    let mut hdrs = [httparse::EMPTY_HEADER; 32];
    let mut resp = httparse::Response::new(&mut hdrs);
    match resp.parse(headers.as_bytes()) {
        Ok(httparse::Status::Complete(_)) | Ok(httparse::Status::Partial) => resp.code.unwrap_or(0),
        Err(_) => 0,
    }
}

/// `Content-Length` header value, if present and valid.
pub fn parse_content_length(headers: &str) -> Option<u32> {
    let mut hdrs = [httparse::EMPTY_HEADER; 32];
    let mut resp = httparse::Response::new(&mut hdrs);
    let _ = resp.parse(headers.as_bytes());
    for h in resp.headers.iter() {
        if h.name.eq_ignore_ascii_case("content-length") {
            let Ok(s) = core::str::from_utf8(h.value) else {
                return None;
            };
            return s.trim().parse().ok();
        }
    }
    None
}

/// `Location` header value (redirect target), if present.
pub fn parse_location<'a>(headers: &'a str) -> Option<&'a str> {
    let mut hdrs = [httparse::EMPTY_HEADER; 32];
    let mut resp = httparse::Response::new(&mut hdrs);
    let _ = resp.parse(headers.as_bytes());
    for h in resp.headers.iter() {
        if h.name.eq_ignore_ascii_case("location") {
            return core::str::from_utf8(h.value).ok().map(str::trim);
        }
    }
    None
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn status_and_headers() {
        let head =
            "HTTP/1.1 302 Found\r\nContent-Length: 12\r\nLocation: https://example.com/a\r\n\r\n";
        assert_eq!(parse_status(head), 302);
        assert_eq!(parse_content_length(head), Some(12));
        assert_eq!(parse_location(head), Some("https://example.com/a"));
    }
}
