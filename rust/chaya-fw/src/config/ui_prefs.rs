//! UI prefs in NVS `cfg` (LED, reset period, lang, theme).

use crate::config::NS_CFG;
use crate::util::platform::KvStore;

const KEY_LED: &str = "led_en";
const KEY_RST: &str = "rstPeriod";
const KEY_LANG: &str = "ui_lang";
const KEY_THEME: &str = "ui_theme";

#[derive(Clone, Debug)]
pub struct UiPrefs {
    pub led_enabled: bool,
    pub reset_days: u8,
    pub lang: heapless::String<4>,
    pub theme: heapless::String<8>,
}

impl Default for UiPrefs {
    fn default() -> Self {
        Self::defaults()
    }
}

impl UiPrefs {
    pub fn defaults() -> Self {
        let mut lang = heapless::String::new();
        let _ = lang.push_str("de");
        let mut theme = heapless::String::new();
        let _ = theme.push_str("system");
        Self {
            led_enabled: true,
            reset_days: 0,
            lang,
            theme,
        }
    }

    pub fn load(kv: &mut dyn KvStore) -> Self {
        let mut cfg = Self::defaults();
        if let Some(v) = kv.get_i32(NS_CFG, KEY_LED) {
            cfg.led_enabled = v != 0;
        }
        if let Some(v) = kv.get_i32(NS_CFG, KEY_RST) {
            if (0..=30).contains(&v) {
                cfg.reset_days = v as u8;
            }
        }
        let mut buf = [0u8; 8];
        if let Some(n) = kv.get_blob(NS_CFG, KEY_LANG, &mut buf) {
            if let Ok(s) = core::str::from_utf8(&buf[..n]) {
                if s == "de" || s == "en" {
                    cfg.lang.clear();
                    let _ = cfg.lang.push_str(s);
                }
            }
        }
        if let Some(n) = kv.get_blob(NS_CFG, KEY_THEME, &mut buf) {
            if let Ok(s) = core::str::from_utf8(&buf[..n]) {
                if s == "system" || s == "dark" || s == "light" {
                    cfg.theme.clear();
                    let _ = cfg.theme.push_str(s);
                }
            }
        }
        cfg
    }

    pub fn save(&self, kv: &mut dyn KvStore) -> bool {
        kv.put_i32(NS_CFG, KEY_LED, if self.led_enabled { 1 } else { 0 })
            && kv.put_i32(NS_CFG, KEY_RST, self.reset_days as i32)
            && kv.put_blob(NS_CFG, KEY_LANG, self.lang.as_bytes())
            && kv.put_blob(NS_CFG, KEY_THEME, self.theme.as_bytes())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::util::platform::SimKv;

    #[test]
    fn defaults_and_roundtrip() {
        let mut kv = SimKv::default();
        let mut prefs = UiPrefs::defaults();
        assert!(prefs.led_enabled);
        assert_eq!(prefs.reset_days, 0);
        assert_eq!(prefs.lang.as_str(), "de");
        assert_eq!(prefs.theme.as_str(), "system");

        prefs.led_enabled = false;
        prefs.reset_days = 14;
        prefs.lang.clear();
        let _ = prefs.lang.push_str("en");
        prefs.theme.clear();
        let _ = prefs.theme.push_str("dark");
        assert!(prefs.save(&mut kv));

        let loaded = UiPrefs::load(&mut kv);
        assert!(!loaded.led_enabled);
        assert_eq!(loaded.reset_days, 14);
        assert_eq!(loaded.lang.as_str(), "en");
        assert_eq!(loaded.theme.as_str(), "dark");
    }
}
