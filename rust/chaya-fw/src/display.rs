//! E-Paper UI orchestration (panel driver integration pending — see RUST_ESP_HAL.md).

use chaya_pure::display::{
    display_battery_icon, display_heart_icon_decide, display_view_for_heart_icon, DisplayHeartIcon,
    DisplayLinkState, DisplayView, HEART_REDRAW_MIN_INTERVAL_MS,
};

pub use chaya_pure::display::{DisplayBatteryIcon, DisplayHeartRedrawDecision};

#[derive(Clone, Debug)]
pub struct DisplayState {
    pub view: DisplayView,
    pub content_allowed: bool,
    pub link: DisplayLinkState,
    pub last_icon: DisplayHeartIcon,
    pub last_drawn_rx: i32,
    pub last_drawn_tx: i32,
    pub last_enqueue_ms: u32,
}

impl Default for DisplayState {
    fn default() -> Self {
        Self {
            view: DisplayView::Unknown,
            content_allowed: false,
            link: DisplayLinkState::default(),
            last_icon: DisplayHeartIcon::Filled,
            last_drawn_rx: 0,
            last_drawn_tx: 0,
            last_enqueue_ms: 0,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DisplayCmd {
    DrawHeart,
    DrawSplash,
    DrawPowerOff,
}

/// What the display task should paint (no SPI yet).
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct DisplayFrame {
    pub cmd: DisplayCmd,
    pub view: DisplayView,
    pub heart_icon: DisplayHeartIcon,
    pub battery_icon: DisplayBatteryIcon,
    pub shown_rx: i32,
    pub shown_tx: i32,
}

pub struct DisplayService {
    pub state: DisplayState,
}

impl Default for DisplayService {
    fn default() -> Self {
        Self {
            state: DisplayState::default(),
        }
    }
}

impl DisplayService {
    pub fn set_content_allowed(&mut self, allowed: bool) {
        self.state.content_allowed = allowed;
    }

    pub fn request_splash(&mut self) -> DisplayFrame {
        self.state.view = DisplayView::SetupQr;
        DisplayFrame {
            cmd: DisplayCmd::DrawSplash,
            view: DisplayView::SetupQr,
            heart_icon: DisplayHeartIcon::Filled,
            battery_icon: DisplayBatteryIcon::Full,
            shown_rx: 0,
            shown_tx: 0,
        }
    }

    pub fn request_heart(
        &mut self,
        ap_mode: bool,
        wifi_ok: bool,
        mqtt_ok: bool,
        now_ms: u32,
        grace_ms: u32,
        shown_rx: i32,
        shown_tx: i32,
        battery_pct: i32,
    ) -> Option<DisplayFrame> {
        if !self.state.content_allowed {
            return None;
        }
        let icon = display_heart_icon_decide(
            ap_mode,
            wifi_ok,
            mqtt_ok,
            now_ms,
            grace_ms,
            &mut self.state.link,
        );
        let view = display_view_for_heart_icon(icon);
        self.state.last_icon = icon;
        self.state.view = view;
        self.state.last_drawn_rx = shown_rx;
        self.state.last_drawn_tx = shown_tx;
        self.state.last_enqueue_ms = now_ms;
        Some(DisplayFrame {
            cmd: DisplayCmd::DrawHeart,
            view,
            heart_icon: icon,
            battery_icon: display_battery_icon(battery_pct),
            shown_rx,
            shown_tx,
        })
    }

    pub fn min_redraw_interval_ms() -> u32 {
        HEART_REDRAW_MIN_INTERVAL_MS
    }
}

/// SPI / epdsi bring-up lives here later. Returns why hardware paint is blocked.
pub fn panel_backend_status() -> &'static str {
    "device: epdsi Jd79660/GDEM0154F51H splash+heart in device::epd"
}
