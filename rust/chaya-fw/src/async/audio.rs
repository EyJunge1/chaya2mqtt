//! Audio beep task.
use chaya_fw::audio::AudioHw;
use esp_println::println;
use crate::runtime::state::*;

#[embassy_executor::task]
pub async fn audio_task(mut audio: AudioHw) {
    let _ = audio.init_codec();
    loop {
        let kind = AUDIO_BEEP.wait().await;
        let cfg = AUDIO_CFG.lock().await.clone();
        let synced = *TIME_SYNCED.lock().await;
        let hour = WALL_CLOCK.lock().await.utc_hour().unwrap_or(12);
        let svc = chaya_fw::audio::AudioService { cfg };
        if !svc.may_play(kind, synced, hour) {
            println!("AUDIO: skip {:?} (quiet/disabled)", kind);
            continue;
        }
        audio.beep(kind).await;
    }
}
