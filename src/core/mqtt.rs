//! From `src/mqtt/*_pure.h` and MQTT admin POST validation (`admin_routes_api_mqtt.cpp`).

use super::constants::device_id_syntax_ok;
use super::heart::heart_sent_remote_should_apply;

pub const MQTT_BROKER_MISSING_BACKOFF_MS: u32 = 60_000;
pub const MQTT_BACKOFF_INITIAL_MS: u32 = 30_000;
pub const MQTT_BACKOFF_MAX_MS: u32 = 60_000;
pub const MQTT_WIFI_DOWN_BACKOFF_MS: u32 = 20_000;
pub const MQTT_WIFI_LOST_DURING_TLS_BACKOFF_MS: u32 = 90_000;
pub const MQTT_NTP_RETRY_MS: u32 = 2_000;
pub const MQTT_PUBLISH_ACK_WAIT_MS: u32 = 5_000;
pub const MQTT_WIFI_NETWORK_TIMEOUT_MS: i32 = 5_000;
pub const MQTT_RECONNECT_TIMEOUT_MS: i32 = 60_000;

/// C++ `MqttConfig::topicPub` / `topicSub` buffer size (includes NUL slot).
pub const MQTT_TOPIC_BUF_LEN: usize = 128;
/// C++ `MqttConfig::server` buffer size (includes NUL slot).
pub const MQTT_SERVER_BUF_LEN: usize = 128;

/// Lowercase `A`–`F` in place (C++ `normalizeHexIdInput`). Other bytes unchanged.
pub fn normalize_hex_id(id: &mut [u8]) {
    for b in id.iter_mut() {
        if (b'A'..=b'F').contains(b) {
            *b = *b - b'A' + b'a';
        }
    }
}

/// Empty OR `device_id_syntax_ok` (C++ `pairingIdInputValid`).
pub fn pairing_id_input_valid(pairing: &str) -> bool {
    pairing.is_empty() || device_id_syntax_ok(pairing)
}

/// Valid partner hex, not own ID, and not equal to a non-empty pairing ID
/// (C++ `partnerIdInputValid`).
pub fn partner_id_input_valid(partner: &str, pairing: &str, own_id: &str) -> bool {
    if !device_id_syntax_ok(partner) {
        return false;
    }
    if partner == own_id {
        return false;
    }
    if !pairing.is_empty() && partner == pairing {
        return false;
    }
    true
}

/// Basic MQTT topic rules: non-empty, fits in `max_len` (NUL slot), no spaces/wildcards.
/// C++ `mqttTopicSyntaxOk`.
pub fn mqtt_topic_syntax_ok(topic: &str, max_len: usize) -> bool {
    if topic.is_empty() || max_len == 0 {
        return false;
    }
    if topic.len() >= max_len {
        return false;
    }
    topic.bytes().all(|c| {
        (0x20..=0x7e).contains(&c) && c != b' ' && c != b'#' && c != b'+'
    })
}

/// Broker host field: non-empty, printable ASCII, no spaces/wildcards.
/// C++ `mqttServerSyntaxOk` / `hostFieldSyntaxOk`.
pub fn mqtt_server_syntax_ok(host: &str, max_len: usize) -> bool {
    if host.is_empty() || max_len == 0 || host.len() >= max_len {
        return false;
    }
    host.bytes().all(|c| {
        (0x20..=0x7e).contains(&c) && c != b' ' && c != b'#' && c != b'+'
    })
}

/// Post-derive topic checks from C++ `handleApiMqttPost`.
/// Returns `"topics"` or `"partner"` error keys on failure.
pub fn mqtt_derived_topics_valid(
    topic_pub: &str,
    topic_sub: &str,
    partner_nonempty: bool,
    max_len: usize,
) -> Result<(), &'static str> {
    if !mqtt_topic_syntax_ok(topic_pub, max_len) {
        return Err("topics");
    }
    if partner_nonempty {
        if !mqtt_topic_syntax_ok(topic_sub, max_len) || topic_pub == topic_sub {
            return Err("partner");
        }
    }
    Ok(())
}

pub fn mqtt_settings_apply_should_clear_pending(has_unapplied_pending: bool) -> bool {
    !has_unapplied_pending
}

pub fn mqtt_settings_apply_should_finish(kill_ok: bool) -> bool {
    kill_ok
}

pub fn mqtt_settings_apply_should_defer(epd_active: bool, factory_queued: bool, shutdown: bool) -> bool {
    epd_active || factory_queued || shutdown
}

pub fn mqtt_settings_apply_nothing_pending_needs_retry(kill_coalesce_pending: bool) -> bool {
    kill_coalesce_pending
}

pub fn mqtt_event_is_live(
    ev_client: Option<*const ()>,
    live_client: Option<*const ()>,
    gen_snap: u32,
    gen_now: u32,
) -> bool {
    match (ev_client, live_client) {
        (Some(ev), Some(live)) => ev == live && gen_snap == gen_now,
        _ => false,
    }
}

pub fn mqtt_own_topic_tx_should_apply(
    incoming: i32,
    local_tx: i32,
    publish_in_flight: bool,
    pending_expected: i32,
) -> bool {
    if publish_in_flight && incoming <= pending_expected {
        return false;
    }
    heart_sent_remote_should_apply(incoming, local_tx)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn echo_ignored_while_publish_in_flight() {
        assert!(!mqtt_own_topic_tx_should_apply(5, 4, true, 5));
        assert!(mqtt_own_topic_tx_should_apply(6, 4, true, 5));
        assert!(mqtt_own_topic_tx_should_apply(5, 4, false, 5));
    }

    #[test]
    fn live_client_match() {
        let p = 0x1000 as *const ();
        assert!(mqtt_event_is_live(Some(p), Some(p), 1, 1));
        assert!(!mqtt_event_is_live(Some(p), Some(p), 1, 2));
        assert!(!mqtt_event_is_live(None, Some(p), 1, 1));
    }

    #[test]
    fn normalize_hex_id_lowercases_a_to_f_only() {
        let mut id = *b"A1B2C3XY";
        normalize_hex_id(&mut id);
        assert_eq!(&id, b"a1b2c3XY");
    }

    #[test]
    fn pairing_id_empty_or_valid_hex() {
        assert!(pairing_id_input_valid(""));
        assert!(pairing_id_input_valid("a1b2c3"));
        assert!(!pairing_id_input_valid("A1B2C3"));
        assert!(!pairing_id_input_valid("a1b2c"));
        assert!(!pairing_id_input_valid("gg0000"));
    }

    #[test]
    fn partner_rejects_own_and_pairing() {
        assert!(partner_id_input_valid("f5e6d7", "", "a1b2c3"));
        assert!(partner_id_input_valid("f5e6d7", "c9d8e7", "a1b2c3"));
        assert!(!partner_id_input_valid("a1b2c3", "", "a1b2c3"));
        assert!(!partner_id_input_valid("c9d8e7", "c9d8e7", "a1b2c3"));
        assert!(!partner_id_input_valid("", "", "a1b2c3"));
        assert!(!partner_id_input_valid("F5E6D7", "", "a1b2c3"));
    }

    #[test]
    fn mqtt_topic_syntax_matches_cpp() {
        assert!(mqtt_topic_syntax_ok("chaya2mqtt/a1b2c3", MQTT_TOPIC_BUF_LEN));
        assert!(!mqtt_topic_syntax_ok("", MQTT_TOPIC_BUF_LEN));
        assert!(!mqtt_topic_syntax_ok("bad topic", MQTT_TOPIC_BUF_LEN));
        assert!(!mqtt_topic_syntax_ok("bad#topic", MQTT_TOPIC_BUF_LEN));
        assert!(!mqtt_topic_syntax_ok("bad+topic", MQTT_TOPIC_BUF_LEN));
    }

    #[test]
    fn mqtt_server_syntax_matches_cpp() {
        assert!(mqtt_server_syntax_ok("broker.example.com", MQTT_SERVER_BUF_LEN));
        assert!(!mqtt_server_syntax_ok("", MQTT_SERVER_BUF_LEN));
        assert!(!mqtt_server_syntax_ok("bad host", MQTT_SERVER_BUF_LEN));
        assert!(!mqtt_server_syntax_ok("bad\x7fhost", MQTT_SERVER_BUF_LEN));
    }

    #[test]
    fn derived_topics_reject_pub_eq_sub() {
        assert!(mqtt_derived_topics_valid(
            "chaya2mqtt/a1b2c3",
            "chaya2mqtt/f5e6d7",
            true,
            MQTT_TOPIC_BUF_LEN
        )
        .is_ok());
        assert_eq!(
            mqtt_derived_topics_valid(
                "chaya2mqtt/a1b2c3",
                "chaya2mqtt/a1b2c3",
                true,
                MQTT_TOPIC_BUF_LEN
            ),
            Err("partner")
        );
        assert_eq!(
            mqtt_derived_topics_valid("", "", false, MQTT_TOPIC_BUF_LEN),
            Err("topics")
        );
    }
}
