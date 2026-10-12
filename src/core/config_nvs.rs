//! From `src/config/nvs_blob_load_pure.h` and `nvs_write_gate_pure.h`.

use crate::constants::NVS_NS_CHAYA;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum NvsBlobLoad {
    UseBlob,
    UseDefaults,
}

pub fn nvs_blob_load_decide(blob_len: usize, expected_len: usize) -> NvsBlobLoad {
    if blob_len == expected_len {
        NvsBlobLoad::UseBlob
    } else {
        NvsBlobLoad::UseDefaults
    }
}

/// Recheck after taking NVS mutex (BUG-LIFE-02 / BUG-LIFE-03).
pub fn nvs_write_allowed_after_lock(shutdown: bool, chaya_suspended: bool, ns: Option<&str>) -> bool {
    if shutdown && ns != Some(NVS_NS_CHAYA) {
        return false;
    }
    if chaya_suspended && ns == Some(NVS_NS_CHAYA) {
        return false;
    }
    true
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn blob_len_match() {
        assert_eq!(nvs_blob_load_decide(8, 8), NvsBlobLoad::UseBlob);
        assert_eq!(nvs_blob_load_decide(7, 8), NvsBlobLoad::UseDefaults);
    }

    #[test]
    fn shutdown_allows_chaya_only() {
        assert!(nvs_write_allowed_after_lock(true, false, Some(NVS_NS_CHAYA)));
        assert!(!nvs_write_allowed_after_lock(true, false, Some("wifi")));
        assert!(!nvs_write_allowed_after_lock(false, true, Some(NVS_NS_CHAYA)));
    }
}
