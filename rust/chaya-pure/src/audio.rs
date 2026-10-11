//! From `src/audio/audio_pure.h` and `audio_drain_pure.h`.

use crate::constants::AUDIO_HOUR_MAX;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum AudioKind {
    Tx,
    Rx,
}

/// Quiet window in local hours. Equal endpoints disable quiet hours. Wraps midnight.
pub fn audio_quiet_hours_active(hour: u8, quiet_start: u8, quiet_end: u8) -> bool {
    if quiet_start > AUDIO_HOUR_MAX || quiet_end > AUDIO_HOUR_MAX {
        return false;
    }
    if quiet_start == quiet_end {
        return false;
    }
    if quiet_start < quiet_end {
        hour >= quiet_start && hour < quiet_end
    } else {
        hour >= quiet_start || hour < quiet_end
    }
}

pub fn audio_playback_allowed(
    kind_enabled: bool,
    volume: u8,
    time_synced: bool,
    hour: u8,
    quiet_start: u8,
    quiet_end: u8,
) -> bool {
    if !kind_enabled || volume == 0 {
        return false;
    }
    if !time_synced {
        return true;
    }
    !audio_quiet_hours_active(hour, quiet_start, quiet_end)
}

/// Overflow pending of the same kind as a just-played queue item must not play again.
pub fn audio_should_play_overflow_pending(
    had_queue_play: bool,
    queue_kind: AudioKind,
    pending_kind: AudioKind,
) -> bool {
    !had_queue_play || queue_kind != pending_kind
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn quiet_hours_wrap_midnight() {
        assert!(audio_quiet_hours_active(23, 22, 6));
        assert!(audio_quiet_hours_active(3, 22, 6));
        assert!(!audio_quiet_hours_active(12, 22, 6));
    }

    #[test]
    fn equal_endpoints_disable_quiet() {
        assert!(!audio_quiet_hours_active(10, 8, 8));
    }

    #[test]
    fn overflow_same_kind_skipped_after_queue_play() {
        assert!(!audio_should_play_overflow_pending(
            true,
            AudioKind::Tx,
            AudioKind::Tx
        ));
        assert!(audio_should_play_overflow_pending(
            true,
            AudioKind::Tx,
            AudioKind::Rx
        ));
    }
}
