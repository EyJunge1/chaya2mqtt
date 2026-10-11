//! From `src/led/led_pattern_pure.h`.

#[derive(Clone, Debug, Default)]
pub struct LedPatternRuntime {
    pub on_pulses_left: u8,
    pub on_phase: bool,
    pub on_ms: u16,
    pub off_ms: u16,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct LedPatternAdvanceResult {
    pub done: bool,
    pub led_on: bool,
    pub duration_ms: u16,
}

pub fn led_pattern_normalize(count: &mut u8, on_ms: &mut u16, _off_ms: &mut u16) {
    if *count == 0 {
        *count = 1;
    }
    if *on_ms == 0 {
        *on_ms = 1;
    }
}

pub fn led_pattern_begin(rt: &mut LedPatternRuntime, mut count: u8, mut on_ms: u16, mut off_ms: u16) -> bool {
    led_pattern_normalize(&mut count, &mut on_ms, &mut off_ms);
    rt.on_pulses_left = count;
    rt.on_phase = true;
    rt.on_ms = on_ms;
    rt.off_ms = off_ms;
    true
}

pub fn led_pattern_advance(rt: &mut LedPatternRuntime) -> LedPatternAdvanceResult {
    if rt.on_phase {
        rt.on_pulses_left = rt.on_pulses_left.saturating_sub(1);
        if rt.on_pulses_left == 0 {
            if rt.off_ms == 0 {
                return LedPatternAdvanceResult {
                    done: true,
                    led_on: false,
                    duration_ms: 0,
                };
            }
            rt.on_phase = false;
            return LedPatternAdvanceResult {
                done: false,
                led_on: false,
                duration_ms: rt.off_ms,
            };
        }
        if rt.off_ms == 0 {
            return LedPatternAdvanceResult {
                done: false,
                led_on: true,
                duration_ms: rt.on_ms,
            };
        }
        rt.on_phase = false;
        return LedPatternAdvanceResult {
            done: false,
            led_on: false,
            duration_ms: rt.off_ms,
        };
    }

    if rt.on_pulses_left == 0 {
        return LedPatternAdvanceResult {
            done: true,
            led_on: false,
            duration_ms: 0,
        };
    }
    rt.on_phase = true;
    LedPatternAdvanceResult {
        done: false,
        led_on: true,
        duration_ms: rt.on_ms,
    }
}

pub fn led_refresh_end_after_applies(hold: bool) -> bool {
    !hold
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn single_pulse_no_off() {
        let mut rt = LedPatternRuntime::default();
        assert!(led_pattern_begin(&mut rt, 1, 50, 0));
        let r = led_pattern_advance(&mut rt);
        assert!(r.done);
        assert!(!r.led_on);
    }
}
