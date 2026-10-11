//! MQTT session task.
use chaya_fw::mqtt::session::{self as mqtt_tcp, EspRng};
use embassy_net::Stack;
use crate::async_rt::state::*;

#[embassy_executor::task]
pub async fn mqtt_session_task(
    stack: Stack<'static>,
    host: &'static str,
    port: u16,
    client_id: &'static str,
    user: &'static str,
    pass: &'static str,
    pub_topic: &'static str,
    sub_topic: &'static str,
    own_topic: &'static str,
    lwt_topic: &'static str,
    device_id: &'static str,
    rng: EspRng,
) {
    mqtt_tcp::session_loop(
        stack,
        host,
        port,
        client_id,
        user,
        pass,
        pub_topic,
        sub_topic,
        own_topic,
        lwt_topic,
        device_id,
        &HEART_BUMP,
        &HEART_RX,
        &HEART_TX,
        &MQTT_OK,
        &AUDIO_BEEP,
        &HEART_DIRTY,
        &MQTT_UP,
        &LED_PRESET,
        rng,
    )
    .await
}
