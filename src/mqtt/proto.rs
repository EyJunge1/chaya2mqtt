//! MQTT 3.1.1 packet builders/parsers.

/// Heapless packet buffer capacity (CONNECT + LWT + credentials).
pub const MQTT_PKT_CAP: usize = 512;

pub type MqttPkt = heapless::Vec<u8, MQTT_PKT_CAP>;

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ParsedMqtt<'a> {
    Connack {
        return_code: u8,
    },
    Puback {
        packet_id: u16,
    },
    Publish {
        topic: &'a [u8],
        payload: &'a [u8],
        payload_i32: Option<i32>,
        packet_id: Option<u16>,
        qos: u8,
    },
    Pingresp,
    Other {
        kind: u8,
    },
}

pub fn push_remaining_length(mut x: usize, out: &mut MqttPkt) {
    loop {
        let mut enc = (x % 128) as u8;
        x /= 128;
        if x > 0 {
            enc |= 0x80;
        }
        let _ = out.push(enc);
        if x == 0 {
            break;
        }
    }
}

pub fn push_mqtt_string(out: &mut MqttPkt, s: &str) {
    let b = s.as_bytes();
    let _ = out.push((b.len() >> 8) as u8);
    let _ = out.push((b.len() & 0xff) as u8);
    let _ = out.extend_from_slice(b);
}

/// CONNECT with Last Will: topic `lwt_topic`, payload `offline`, Will QoS1 + Will Retain.
/// Flags: clean=0x02, will=0x04, will qos1=0x08, will retain=0x20 (+ user/pass).
pub fn build_connect(client_id: &str, user: &str, pass: &str, lwt_topic: &str) -> MqttPkt {
    let mut vh = MqttPkt::new();
    push_mqtt_string(&mut vh, "MQTT");
    let _ = vh.push(0x04); // protocol level 4 (3.1.1)
    let mut flags = 0x02u8; // clean session
    if !lwt_topic.is_empty() {
        flags |= 0x04 | 0x08 | 0x20; // will + will qos1 + will retain
    }
    if !user.is_empty() {
        flags |= 0x80;
    }
    if !pass.is_empty() {
        flags |= 0x40;
    }
    let _ = vh.push(flags);
    let _ = vh.push(0x00);
    let _ = vh.push(0x3c); // keep-alive 60s
    push_mqtt_string(&mut vh, client_id);
    if !lwt_topic.is_empty() {
        push_mqtt_string(&mut vh, lwt_topic);
        push_mqtt_string(&mut vh, "offline");
    }
    if !user.is_empty() {
        push_mqtt_string(&mut vh, user);
    }
    if !pass.is_empty() {
        push_mqtt_string(&mut vh, pass);
    }
    let mut pkt = MqttPkt::new();
    let _ = pkt.push(0x10);
    push_remaining_length(vh.len(), &mut pkt);
    let _ = pkt.extend_from_slice(&vh);
    pkt
}

/// SUBSCRIBE with requested QoS1 (0x01).
pub fn build_subscribe(packet_id: u16, topic: &str) -> MqttPkt {
    let mut vh = MqttPkt::new();
    let _ = vh.push((packet_id >> 8) as u8);
    let _ = vh.push((packet_id & 0xff) as u8);
    push_mqtt_string(&mut vh, topic);
    let _ = vh.push(0x01); // requested QoS 1
    let mut pkt = MqttPkt::new();
    let _ = pkt.push(0x82);
    push_remaining_length(vh.len(), &mut pkt);
    let _ = pkt.extend_from_slice(&vh);
    pkt
}

/// PUBLISH retained QoS1 (fixed header 0x33) with packet_id in the variable header.
pub fn build_publish_qos1_retain(topic: &str, payload: &str, packet_id: u16) -> MqttPkt {
    let mut vh = MqttPkt::new();
    push_mqtt_string(&mut vh, topic);
    let _ = vh.push((packet_id >> 8) as u8);
    let _ = vh.push((packet_id & 0xff) as u8);
    let _ = vh.extend_from_slice(payload.as_bytes());
    let mut pkt = MqttPkt::new();
    let _ = pkt.push(0x33); // PUBLISH | QoS1 | RETAIN
    push_remaining_length(vh.len(), &mut pkt);
    let _ = pkt.extend_from_slice(&vh);
    pkt
}

pub fn build_puback(packet_id: u16) -> MqttPkt {
    let mut pkt = MqttPkt::new();
    let _ = pkt.push(0x40);
    let _ = pkt.push(0x02);
    let _ = pkt.push((packet_id >> 8) as u8);
    let _ = pkt.push((packet_id & 0xff) as u8);
    pkt
}

pub fn parse_i32(s: &[u8]) -> Option<i32> {
    let Ok(t) = core::str::from_utf8(s) else {
        return None;
    };
    let t = t.trim();
    if t.is_empty() {
        return None;
    }
    let mut neg = false;
    let mut rest = t;
    if let Some(r) = t.strip_prefix('-') {
        neg = true;
        rest = r;
    }
    let mut n: i32 = 0;
    for c in rest.bytes() {
        if !c.is_ascii_digit() {
            return None;
        }
        n = n.saturating_mul(10).saturating_add((c - b'0') as i32);
    }
    Some(if neg { -n } else { n })
}

fn decode_remaining_length(buf: &[u8]) -> Option<(usize, usize)> {
    if buf.len() < 2 {
        return None;
    }
    let mut multiplier = 1usize;
    let mut value = 0usize;
    let mut i = 1usize;
    loop {
        if i >= buf.len() {
            return None;
        }
        let enc = buf[i];
        i += 1;
        value += (enc & 0x7f) as usize * multiplier;
        multiplier *= 128;
        if enc & 0x80 == 0 {
            break;
        }
        if i > 5 {
            return None;
        }
    }
    Some((i, value))
}

pub fn consume_packet(buf: &[u8]) -> Option<(usize, ParsedMqtt<'_>)> {
    let (vh_start, rem_len) = decode_remaining_length(buf)?;
    let total = vh_start + rem_len;
    if buf.len() < total {
        return None;
    }
    let fixed = buf[0];
    let kind = fixed >> 4;
    let payload = &buf[vh_start..total];
    let parsed = match kind {
        2 => {
            // CONNACK: session present + return code
            let rc = if payload.len() >= 2 { payload[1] } else { 0xff };
            ParsedMqtt::Connack { return_code: rc }
        }
        3 => {
            let qos = (fixed >> 1) & 0x03;
            parse_publish(payload, qos).unwrap_or(ParsedMqtt::Other { kind })
        }
        4 => {
            let packet_id = if payload.len() >= 2 {
                ((payload[0] as u16) << 8) | payload[1] as u16
            } else {
                0
            };
            ParsedMqtt::Puback { packet_id }
        }
        13 => ParsedMqtt::Pingresp,
        _ => ParsedMqtt::Other { kind },
    };
    Some((total, parsed))
}

fn parse_publish(payload: &[u8], qos: u8) -> Option<ParsedMqtt<'_>> {
    if payload.len() < 2 {
        return None;
    }
    let tlen = ((payload[0] as usize) << 8) | payload[1] as usize;
    if payload.len() < 2 + tlen {
        return None;
    }
    let topic = &payload[2..2 + tlen];
    let mut off = 2 + tlen;
    let packet_id = if qos > 0 {
        if payload.len() < off + 2 {
            return None;
        }
        let id = ((payload[off] as u16) << 8) | payload[off + 1] as u16;
        off += 2;
        Some(id)
    } else {
        None
    };
    let body = &payload[off..];
    Some(ParsedMqtt::Publish {
        topic,
        payload: body,
        payload_i32: parse_i32(body),
        packet_id,
        qos,
    })
}

pub fn itoa_into(s: &mut heapless::String<16>, mut n: i32) {
    if n < 0 {
        let _ = s.push('-');
        n = -n;
    }
    if n == 0 {
        let _ = s.push('0');
        return;
    }
    let mut digs = heapless::Vec::<u8, 16>::new();
    while n > 0 {
        let _ = digs.push(b'0' + (n % 10) as u8);
        n /= 10;
    }
    for d in digs.iter().rev() {
        let _ = s.push(*d as char);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn connect_includes_lwt_flags_and_fields() {
        let pkt = build_connect("abcdef", "u", "p", "chaya2mqtt/abcdef/lwt");
        assert_eq!(pkt[0], 0x10);
        // flags byte follows protocol name "MQTT" (2+4) + level (1) → index after remaining length
        let mut i = 1usize;
        while pkt[i] & 0x80 != 0 {
            i += 1;
        }
        i += 1; // past remaining-length bytes
        // "MQTT" string + level
        i += 2 + 4 + 1;
        let flags = pkt[i];
        assert_eq!(flags & 0x02, 0x02); // clean
        assert_eq!(flags & 0x04, 0x04); // will
        assert_eq!(flags & 0x08, 0x08); // will qos1
        assert_eq!(flags & 0x20, 0x20); // will retain
        assert_eq!(flags & 0x80, 0x80); // user
        assert_eq!(flags & 0x40, 0x40); // pass
        assert!(pkt.windows(b"chaya2mqtt/abcdef/lwt".len()).any(|w| w == b"chaya2mqtt/abcdef/lwt"));
        assert!(pkt.windows(b"offline".len()).any(|w| w == b"offline"));
    }

    #[test]
    fn subscribe_requests_qos1() {
        let pkt = build_subscribe(1, "chaya2mqtt/partner");
        assert_eq!(pkt[0], 0x82);
        assert_eq!(*pkt.last().unwrap(), 0x01);
    }

    #[test]
    fn publish_qos1_retain_has_packet_id() {
        let pkt = build_publish_qos1_retain("chaya2mqtt/abcdef", "42", 7);
        assert_eq!(pkt[0], 0x33);
        let (used, parsed) = consume_packet(&pkt).expect("parse");
        assert_eq!(used, pkt.len());
        match parsed {
            ParsedMqtt::Publish {
                topic,
                payload_i32,
                packet_id,
                qos,
                ..
            } => {
                assert_eq!(topic, b"chaya2mqtt/abcdef");
                assert_eq!(payload_i32, Some(42));
                assert_eq!(packet_id, Some(7));
                assert_eq!(qos, 1);
            }
            other => panic!("unexpected {other:?}"),
        }
    }

    #[test]
    fn consume_qos1_publish_skips_packet_id_for_payload() {
        // Manual QoS1 PUBLISH: topic "t", id 0x0009, payload "3"
        let mut pkt = MqttPkt::new();
        let _ = pkt.push(0x32); // PUBLISH QoS1 no retain
        let _ = pkt.push(6); // remaining: tlen(2)+t(1)+pid(2)+payload(1)
        let _ = pkt.push(0);
        let _ = pkt.push(1);
        let _ = pkt.push(b't');
        let _ = pkt.push(0);
        let _ = pkt.push(9);
        let _ = pkt.push(b'3');
        let (_, parsed) = consume_packet(&pkt).unwrap();
        match parsed {
            ParsedMqtt::Publish {
                payload_i32,
                packet_id,
                qos,
                ..
            } => {
                assert_eq!(payload_i32, Some(3));
                assert_eq!(packet_id, Some(9));
                assert_eq!(qos, 1);
            }
            other => panic!("unexpected {other:?}"),
        }
    }

    #[test]
    fn puback_roundtrip() {
        let pkt = build_puback(0x1234);
        let (_, parsed) = consume_packet(&pkt).unwrap();
        assert_eq!(parsed, ParsedMqtt::Puback { packet_id: 0x1234 });
    }
}
