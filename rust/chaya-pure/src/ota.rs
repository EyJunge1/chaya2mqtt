//! From `src/ota/ota_queue_pure.h`, `ota_health.h`, `version_cmp.h`, `ota_url_allow.h`.

/// Stable STA runtime before cancelling OTA rollback (ms).
pub const OTA_HEALTH_STABLE_MS: u32 = 30_000;

const DOWNLOAD_PREFIX: &str = "https://github.com/EyJunge1/chaya2mqtt/releases/download/";

const CDN_HOSTS: &[&str] = &[
    "objects.githubusercontent.com",
    "release-assets.githubusercontent.com",
    "github-releases.githubusercontent.com",
];

/// Parsed CalVer / beta (`-rc.N`) tag.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct OtaParsedVersion {
    pub major: u32,
    pub minor: u32,
    pub patch: u32,
    pub rc: u32,
    pub is_rc: bool,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum OtaDownloadAsset {
    Firmware,
    Sha512,
}

/// `runGithubCheck` may replace the copied release only when no install is queued.
pub fn ota_github_check_may_clear_pending_release(install_queued: bool) -> bool {
    !install_queued
}

/// Decide whether an automatic once-per-UTC-day GitHub check may run (C++ `maybeDailyCheck`).
///
/// Never auto-installs — callers only probe for available updates.
pub fn ota_daily_check_due(
    app_version: &str,
    sta_connected: bool,
    ap_mode: bool,
    ntp_synced: bool,
    busy: bool,
    last_check_day: u32,
    today_utc_day: u32,
) -> bool {
    if ap_mode || !sta_connected {
        return false;
    }
    if app_version == "dev" {
        return false;
    }
    if !ntp_synced || busy {
        return false;
    }
    last_check_day != today_utc_day
}

/// True once setup is complete, boot WiFi settled, STA linked, and `window_ms` elapsed
/// since `settled_at_ms` (wrap-safe via wrapping subtract).
pub fn ota_health_window_elapsed(
    setup_complete: bool,
    boot_settled: bool,
    sta_connected: bool,
    settled_at_ms: u32,
    now_ms: u32,
    window_ms: u32,
) -> bool {
    if !setup_complete || !boot_settled || !sta_connected || settled_at_ms == 0 {
        return false;
    }
    now_ms.wrapping_sub(settled_at_ms) >= window_ms
}

fn parse_uint_token(s: &str, max_val: u32) -> Option<(u32, usize)> {
    if s.is_empty() || !s.as_bytes()[0].is_ascii_digit() {
        return None;
    }
    let mut end = 0usize;
    for (i, b) in s.bytes().enumerate() {
        if b.is_ascii_digit() {
            end = i + 1;
        } else {
            break;
        }
    }
    let Ok(v) = s[..end].parse::<u32>() else {
        return None;
    };
    if v > max_val {
        return None;
    }
    Some((v, end))
}

/// Parse CalVer `vYYYY.M.PATCH[-rc.N]` (leading `v`/`V` optional).
pub fn ota_version_parse(tag: &str) -> Option<OtaParsedVersion> {
    if tag.is_empty() {
        return None;
    }
    let mut p = tag;
    if p.as_bytes()[0] == b'v' || p.as_bytes()[0] == b'V' {
        p = &p[1..];
    }
    let (major, n) = parse_uint_token(p, 9999)?;
    p = &p[n..];
    if !p.starts_with('.') {
        return None;
    }
    p = &p[1..];
    let (minor, n) = parse_uint_token(p, 999)?;
    p = &p[n..];
    if !p.starts_with('.') {
        return None;
    }
    p = &p[1..];
    let (patch, n) = parse_uint_token(p, 999)?;
    p = &p[n..];
    if p.is_empty() {
        return Some(OtaParsedVersion {
            major,
            minor,
            patch,
            rc: 0,
            is_rc: false,
        });
    }
    if !(p.starts_with("-rc.") || p.starts_with("-RC.")) {
        return None;
    }
    p = &p[4..];
    let (rc, n) = parse_uint_token(p, 9999)?;
    p = &p[n..];
    if !p.is_empty() || rc == 0 {
        return None;
    }
    Some(OtaParsedVersion {
        major,
        minor,
        patch,
        rc,
        is_rc: true,
    })
}

pub fn ota_version_is_rc(tag: &str) -> bool {
    ota_version_parse(tag).is_some_and(|p| p.is_rc)
}

/// Strict release-tag format shared with CI: `vYYYY.M.PATCH[-rc.N]`.
pub fn ota_release_tag_is_allowed(tag: &str) -> bool {
    let bytes = tag.as_bytes();
    if bytes.first() != Some(&b'v') {
        return false;
    }
    let mut i = 1usize;
    for _ in 0..4 {
        if i >= bytes.len() || !bytes[i].is_ascii_digit() {
            return false;
        }
        i += 1;
    }
    if i >= bytes.len() || bytes[i] != b'.' {
        return false;
    }
    i += 1;
    // Month: 1-9 or 10-12 (no leading zero).
    if i < bytes.len()
        && bytes[i] >= b'1'
        && bytes[i] <= b'9'
        && i + 1 < bytes.len()
        && bytes[i + 1] == b'.'
    {
        i += 2;
    } else if i + 2 < bytes.len()
        && bytes[i] == b'1'
        && bytes[i + 1] >= b'0'
        && bytes[i + 1] <= b'2'
        && bytes[i + 2] == b'.'
    {
        i += 3;
    } else {
        return false;
    }
    if i >= bytes.len() || !bytes[i].is_ascii_digit() {
        return false;
    }
    while i < bytes.len() && bytes[i].is_ascii_digit() {
        i += 1;
    }
    if i == bytes.len() {
        return true;
    }
    if !tag[i..].starts_with("-rc.") {
        return false;
    }
    i += 4;
    if i >= bytes.len() || !bytes[i].is_ascii_digit() {
        return false;
    }
    while i < bytes.len() && bytes[i].is_ascii_digit() {
        i += 1;
    }
    i == bytes.len() && ota_version_parse(tag).is_some_and(|p| p.is_rc)
}

/// True if remote is a usable newer version than local (no downgrade).
pub fn ota_version_is_newer(remote_tag: &str, local_version: &str) -> bool {
    let Some(remote) = ota_version_parse(remote_tag) else {
        return false;
    };
    if local_version == "dev" {
        return true;
    }
    let Some(local) = ota_version_parse(local_version) else {
        return false;
    };
    if remote.major != local.major {
        return remote.major > local.major;
    }
    if remote.minor != local.minor {
        return remote.minor > local.minor;
    }
    if remote.patch != local.patch {
        return remote.patch > local.patch;
    }
    if remote.is_rc != local.is_rc {
        // Stable is newer than a beta (-rc.N) with the same base version.
        return !remote.is_rc;
    }
    remote.is_rc && remote.rc > local.rc
}

fn asset_name(asset: OtaDownloadAsset) -> &'static str {
    match asset {
        OtaDownloadAsset::Firmware => "firmware.bin",
        OtaDownloadAsset::Sha512 => "firmware.sha512",
    }
}

/// Exact GitHub release download URL for this repo + asset.
pub fn ota_release_download_url_allowed(url: &str, asset: OtaDownloadAsset) -> bool {
    let Some(rest) = url.strip_prefix(DOWNLOAD_PREFIX) else {
        return false;
    };
    let Some((tag, file)) = rest.split_once('/') else {
        return false;
    };
    if tag.is_empty() || tag.len() >= 64 {
        return false;
    }
    if !ota_release_tag_is_allowed(tag) {
        return false;
    }
    file == asset_name(asset)
}

/// Allowlist for OTA HTTP redirect targets after the initial GitHub release URL.
pub fn ota_release_download_redirect_url_allowed(url: &str) -> bool {
    if url.is_empty() {
        return false;
    }
    if ota_release_download_url_allowed(url, OtaDownloadAsset::Firmware)
        || ota_release_download_url_allowed(url, OtaDownloadAsset::Sha512)
    {
        return true;
    }
    let Some(host_and_rest) = url.strip_prefix("https://") else {
        return false;
    };
    if host_and_rest.is_empty()
        || host_and_rest.as_bytes()[0] == b'/'
        || host_and_rest.as_bytes()[0] == b'@'
    {
        return false;
    }
    // Reject userinfo (https://user:pass@host/...).
    let path_at = host_and_rest.find('/');
    if let Some(at) = host_and_rest.find('@') {
        if path_at.map(|p| at < p).unwrap_or(true) {
            return false;
        }
    }
    for cdn in CDN_HOSTS {
        if let Some(rest) = host_and_rest.strip_prefix(cdn) {
            let next = rest.as_bytes().first().copied().unwrap_or(0);
            // Exact host, optional port, path, or query.
            return next == 0 || next == b'/' || next == b':' || next == b'?';
        }
    }
    false
}

fn extract_tag_at<'a>(body: &'a str, from: usize) -> Option<(usize, &'a str)> {
    let rest = &body[from..];
    let key_rel = rest.find("\"tag_name\"")?;
    let after_key_rel = key_rel + "\"tag_name\"".len();
    let after_key = &rest[after_key_rel..];
    let colon_rel = after_key.find(':')?;
    let raw_value = &after_key[colon_rel + 1..];
    let trimmed = raw_value.trim_start();
    let trimmed_off = raw_value.len() - trimmed.len();
    let value = trimmed.strip_prefix('"')?;
    let end = value.find('"')?;
    if end == 0 || end > 31 {
        return None;
    }
    let tag_abs = from + after_key_rel + colon_rel + 1 + trimmed_off + 1;
    let tag = &body[tag_abs..tag_abs + end];
    Some((tag_abs + end + 1, tag))
}

/// Scan a GitHub `/releases` JSON list and pick the best allowed tag.
///
/// `prefer_prerelease`: newest allowed `-rc.N` tag, else newest allowed stable.
/// Otherwise: newest allowed stable (non-rc) tag.
pub fn ota_select_best_release_tag(body: &str, prefer_prerelease: bool) -> Option<&str> {
    let mut best_stable: Option<&str> = None;
    let mut best_rc: Option<&str> = None;
    let mut from = 0usize;
    while let Some((next, tag)) = extract_tag_at(body, from) {
        from = next;
        if !ota_release_tag_is_allowed(tag) {
            continue;
        }
        if ota_version_is_rc(tag) {
            if best_rc.map(|b| ota_version_is_newer(tag, b)).unwrap_or(true) {
                best_rc = Some(tag);
            }
        } else if best_stable
            .map(|b| ota_version_is_newer(tag, b))
            .unwrap_or(true)
        {
            best_stable = Some(tag);
        }
    }
    if prefer_prerelease {
        best_rc.or(best_stable)
    } else {
        best_stable
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn clear_only_when_idle() {
        assert!(ota_github_check_may_clear_pending_release(false));
        assert!(!ota_github_check_may_clear_pending_release(true));
    }

    #[test]
    fn daily_check_gates() {
        assert!(ota_daily_check_due(
            "v2026.10.1", true, false, true, false, 0, 20_000
        ));
        assert!(!ota_daily_check_due(
            "v2026.10.1", true, false, true, false, 20_000, 20_000
        ));
        assert!(!ota_daily_check_due(
            "dev", true, false, true, false, 0, 20_000
        ));
        assert!(!ota_daily_check_due(
            "v2026.10.1", false, false, true, false, 0, 20_000
        ));
        assert!(!ota_daily_check_due(
            "v2026.10.1", true, true, true, false, 0, 20_000
        ));
        assert!(!ota_daily_check_due(
            "v2026.10.1", true, false, false, false, 0, 20_000
        ));
        assert!(!ota_daily_check_due(
            "v2026.10.1", true, false, true, true, 0, 20_000
        ));
    }

    #[test]
    fn health_needs_sta_and_window() {
        assert!(!ota_health_window_elapsed(
            true,
            true,
            false,
            1,
            40_000,
            OTA_HEALTH_STABLE_MS
        ));
        assert!(!ota_health_window_elapsed(
            true,
            true,
            true,
            0,
            40_000,
            OTA_HEALTH_STABLE_MS
        ));
        assert!(ota_health_window_elapsed(
            true,
            true,
            true,
            1,
            30_001,
            OTA_HEALTH_STABLE_MS
        ));
    }

    #[test]
    fn version_compare_matches_cpp() {
        assert!(ota_version_is_newer("v2026.8.2", "2026.8.1"));
        assert!(!ota_version_is_newer("v2026.8.1", "2026.8.1"));
        assert!(!ota_version_is_newer("v2026.8.0", "2026.8.1"));
        assert!(ota_version_is_newer("v2026.8.1", "2026.8.1-rc.1"));
        assert!(!ota_version_is_newer("v2026.8.1-rc.2", "2026.8.1"));
        assert!(ota_version_is_newer("v2026.8.1-rc.2", "2026.8.1-rc.1"));
        assert!(ota_version_is_rc("2026.8.1-rc.1"));
        assert!(!ota_version_is_rc("2026.8.1"));
        assert!(ota_version_is_newer("v2062.1.0", "2061.12.999"));
        assert!(ota_version_is_newer("v2026.8.1", "dev"));
        assert!(!ota_version_is_newer("v2026.8", "2026.7.1"));
        assert!(!ota_version_is_newer("v2026.8.1-rc.1-extra", "2026.8.0"));
        assert!(ota_release_tag_is_allowed("v2026.8.1"));
        assert!(ota_release_tag_is_allowed("v2026.12.0-rc.1"));
        assert!(!ota_release_tag_is_allowed("2026.8.1"));
        assert!(!ota_release_tag_is_allowed("V2026.8.1"));
        assert!(!ota_release_tag_is_allowed("v2026.0.1"));
        assert!(!ota_release_tag_is_allowed("v2026.13.1"));
        assert!(!ota_release_tag_is_allowed("v2026.08.1"));
        assert!(!ota_release_tag_is_allowed("v2026.8.1-rc.0"));
    }

    #[test]
    fn download_url_allowlist_matches_cpp() {
        assert!(ota_release_download_url_allowed(
            "https://github.com/EyJunge1/chaya2mqtt/releases/download/v2026.8.1/firmware.bin",
            OtaDownloadAsset::Firmware
        ));
        assert!(ota_release_download_url_allowed(
            "https://github.com/EyJunge1/chaya2mqtt/releases/download/v2026.8.1-rc.1/firmware.sha512",
            OtaDownloadAsset::Sha512
        ));
        assert!(!ota_release_download_url_allowed(
            "https://github.com/EyJunge1/chaya2mqtt/releases/download/v2026.8.1/firmware.sha256",
            OtaDownloadAsset::Sha512
        ));
        assert!(!ota_release_download_url_allowed(
            "http://github.com/EyJunge1/chaya2mqtt/releases/download/v2026.8.1/firmware.bin",
            OtaDownloadAsset::Firmware
        ));
        assert!(!ota_release_download_url_allowed(
            "https://evil.example/EyJunge1/chaya2mqtt/releases/download/v2026.8.1/firmware.bin",
            OtaDownloadAsset::Firmware
        ));
        assert!(!ota_release_download_url_allowed(
            "https://github.com/EyJunge1/chaya2mqtt/releases/download/v2026.08.1/firmware.bin",
            OtaDownloadAsset::Firmware
        ));
        assert!(!ota_release_download_url_allowed(
            "https://github.com/EyJunge1/chaya2mqtt/releases/download/v2026.8.1/firmware.factory.bin",
            OtaDownloadAsset::Firmware
        ));
        assert!(!ota_release_download_url_allowed(
            "https://github.com/EyJunge1/chaya2mqtt/releases/download/v2026.8.1/../firmware.bin",
            OtaDownloadAsset::Firmware
        ));
        assert!(!ota_release_download_url_allowed(
            "https://github.com/EyJunge1/chaya2mqtt/releases/download/v2026.8.1/firmware.bin?x=1",
            OtaDownloadAsset::Firmware
        ));
        assert!(!ota_release_download_url_allowed(
            "https://github.com/EyJunge1/chaya2mqtt/releases/download/v2026.8.1/firmware.bin#frag",
            OtaDownloadAsset::Firmware
        ));

        assert!(ota_release_download_redirect_url_allowed(
            "https://github.com/EyJunge1/chaya2mqtt/releases/download/v2026.8.1/firmware.bin"
        ));
        assert!(ota_release_download_redirect_url_allowed(
            "https://objects.githubusercontent.com/github-production-release-asset/1/abc"
        ));
        assert!(ota_release_download_redirect_url_allowed(
            "https://release-assets.githubusercontent.com/github-production-release-asset/1/abc?sig=x"
        ));
        assert!(!ota_release_download_redirect_url_allowed(
            "http://objects.githubusercontent.com/github-production-release-asset/1/abc"
        ));
        assert!(!ota_release_download_redirect_url_allowed(
            "https://evil.example/objects.githubusercontent.com/x"
        ));
        assert!(!ota_release_download_redirect_url_allowed(
            "https://user:pass@objects.githubusercontent.com/x"
        ));
    }

    #[test]
    fn select_best_beta_prefers_newer_rc() {
        let json = r#"[
            {"tag_name":"v2026.8.2-rc.1","draft":false,"prerelease":true},
            {"tag_name":"v2026.8.1","draft":false,"prerelease":false},
            {"tag_name":"v2026.9.1-rc.3","draft":false,"prerelease":true},
            {"tag_name":"v2026.9.1-rc.2","draft":false,"prerelease":true}
        ]"#;
        assert_eq!(
            ota_select_best_release_tag(json, true),
            Some("v2026.9.1-rc.3")
        );
        assert_eq!(
            ota_select_best_release_tag(json, false),
            Some("v2026.8.1")
        );

        let only_stable = r#"[{"tag_name":"v2026.8.1","draft":false,"prerelease":false}]"#;
        assert_eq!(
            ota_select_best_release_tag(only_stable, true),
            Some("v2026.8.1")
        );
    }
}
