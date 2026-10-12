//! Audio beep task.
use crate::async_rt::state::*;
use chaya_fw::audio::AudioHw;
use esp_println::println;

#[embassy_executor::task]
pub async fn audio_task(mut audio: AudioHw) {
    let _ = audio.init_codec();
    loop {
        let kind = AUDIO_BEEP.wait().await;
        let cfg = AUDIO_CFG.lock().await.clone();
        let synced = *TIME_SYNCED.lock().await;
        let hour = WALL_CLOCK.lock().await.utc_hour().unwrap_or(12);
        let svc = chaya_fw::audio::AudioService { cfg: cfg.clone() };
        if !svc.may_play(kind, synced, hour) {
            println!("AUDIO: skip {:?} (quiet/disabled)", kind);
            continue;
        }
        audio.beep(kind, cfg.volume).await;
    }
}
