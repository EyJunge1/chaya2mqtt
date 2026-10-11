//! MQTT 3.1.1 packet builders/parsers.

pub fn push_remaining_length(mut x: usize, out: &mut heapless::Vec<u8, 384>) {
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

pub fn push_mqtt_string(out: &mut heapless::Vec<u8, 384>, s: &str) {
    let b = s.as_bytes();
    let _ = out.push((b.len() >> 8) as u8);
    let _ = out.push((b.len() & 0xff) as u8);
    let _ = out.extend_from_slice(b);
}

pub fn build_connect(client_id: &str, user: &str, pass: &str) -> heapless::Vec<u8, 384> {
    let mut vh = heapless::Vec::<u8, 384>::new();
    push_mqtt_string(&mut vh, "MQTT");
    let _ = vh.push(0x04);
    let mut flags = 0x02u8;
    if !user.is_empty() {
        flags |= 0x80;
    }
    if !pass.is_empty() {
        flags |= 0x40;
    }
    let _ = vh.push(flags);
    let _ = vh.push(0x00);
    let _ = vh.push(0x3c);
    push_mqtt_string(&mut vh, client_id);
    if !user.is_empty() {
        push_mqtt_string(&mut vh, user);
    }
    if !pass.is_empty() {
        push_mqtt_string(&mut vh, pass);
    }
    let mut pkt = heapless::Vec::<u8, 384>::new();
    let _ = pkt.push(0x10);
    push_remaining_length(vh.len(), &mut pkt);
    let _ = pkt.extend_from_slice(&vh);
    pkt
}

pub fn build_subscribe(packet_id: u16, topic: &str) -> heapless::Vec<u8, 384> {
    let mut vh = heapless::Vec::<u8, 384>::new();
    let _ = vh.push((packet_id >> 8) as u8);
    let _ = vh.push((packet_id & 0xff) as u8);
    push_mqtt_string(&mut vh, topic);
    let _ = vh.push(0x00);
    let mut pkt = heapless::Vec::<u8, 384>::new();
    let _ = pkt.push(0x82);
    push_remaining_length(vh.len(), &mut pkt);
    let _ = pkt.extend_from_slice(&vh);
    pkt
}

pub fn build_publish(topic: &str, payload: &str) -> heapless::Vec<u8, 384> {
    let mut vh = heapless::Vec::<u8, 384>::new();
    push_mqtt_string(&mut vh, topic);
    let _ = vh.extend_from_slice(payload.as_bytes());
    let mut pkt = heapless::Vec::<u8, 384>::new();
    let _ = pkt.push(0x30);
    push_remaining_length(vh.len(), &mut pkt);
    let _ = pkt.extend_from_slice(&vh);
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

pub fn consume_packet(buf: &[u8]) -> Option<(usize, Option<i32>)> {
    if buf.len() < 2 {
        return None;
    }
    let kind = buf[0] >> 4;
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
    let total = i + value;
    if buf.len() < total {
        return None;
    }
    let payload = &buf[i..total];
    let mut published = None;
    if kind == 3 && payload.len() >= 2 {
        let tlen = ((payload[0] as usize) << 8) | payload[1] as usize;
        if payload.len() >= 2 + tlen {
            published = parse_i32(&payload[2 + tlen..]);
        }
    }
    Some((total, published))
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
