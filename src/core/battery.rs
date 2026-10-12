//! From `src/battery/battery_pure.h`.

use super::constants::BATTERY_CRITICAL_PCT;

const CURVE: [(i32, i32); 9] = [
    (3300, 0),
    (3500, 5),
    (3600, 10),
    (3700, 20),
    (3800, 35),
    (3900, 55),
    (4000, 75),
    (4100, 90),
    (4200, 100),
];

/// Piecewise-linear LiPo estimate from pack millivolts (0–100).
pub fn battery_pct_from_milli_volts(mv: i32) -> i32 {
    if mv <= CURVE[0].0 {
        return 0;
    }
    let last = CURVE.len() - 1;
    if mv >= CURVE[last].0 {
        return 100;
    }
    for i in 0..last {
        if mv <= CURVE[i + 1].0 {
            let span = CURVE[i + 1].0 - CURVE[i].0;
            let rise = CURVE[i + 1].1 - CURVE[i].1;
            let d = mv - CURVE[i].0;
            return CURVE[i].1 + if span > 0 { (rise * d) / span } else { 0 };
        }
    }
    100
}

pub fn battery_critical_low(pct: i32) -> bool {
    pct < BATTERY_CRITICAL_PCT
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn endpoints() {
        assert_eq!(battery_pct_from_milli_volts(3000), 0);
        assert_eq!(battery_pct_from_milli_volts(4200), 100);
    }

    #[test]
    fn mid_curve() {
        assert_eq!(battery_pct_from_milli_volts(3700), 20);
        assert!(battery_critical_low(9));
        assert!(!battery_critical_low(10));
    }
}
