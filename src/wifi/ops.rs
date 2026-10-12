//! Shared Wi-Fi controller ops: AP scan + SoftAP connection test (APSTA).

use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::mutex::Mutex;
use embassy_sync::signal::Signal;
use embassy_time::{Duration, Timer, with_timeout};
use esp_println::println;
use esp_radio::wifi::{
    AuthenticationMethod, AuthenticationMethodConfig, Config, WifiController,
    ap::AccessPointConfig, scan::ScanConfig, sta::StationConfig,
};

use crate::config::SETUP_AP_SSID;
use crate::wifi::scan::{ScanAp, ScanCache, ScanStatus};
use crate::wifi::WlanConfig;

type Cs = CriticalSectionRawMutex;
pub type WifiMutex = Mutex<Cs, WifiController<'static>>;

pub async fn run_wifi_ops(
    wifi: &'static WifiMutex,
    ap_psk: &'static str,
    scan: &'static Mutex<Cs, ScanCache>,
    pending: &'static Mutex<Cs, WlanConfig>,
    test_state: &'static Mutex<Cs, heapless::String<12>>,
    test_ssid: &'static Mutex<Cs, heapless::String<33>>,
    test_owns: &'static Mutex<Cs, bool>,
    scan_kick: &'static Signal<Cs, ()>,
    test_kick: &'static Signal<Cs, ()>,
) {
    loop {
        match embassy_futures::select::select(scan_kick.wait(), test_kick.wait()).await {
            embassy_futures::select::Either::First(()) => {
                do_scan(wifi, scan).await;
            }
            embassy_futures::select::Either::Second(()) => {
                do_connect_test(wifi, ap_psk, pending, test_state, test_ssid, test_owns).await;
            }
        }
    }
}

pub async fn run_scan_only(
    wifi: &'static WifiMutex,
    scan: &'static Mutex<Cs, ScanCache>,
    scan_kick: &'static Signal<Cs, ()>,
) {
    loop {
        scan_kick.wait().await;
        do_scan(wifi, scan).await;
    }
}

async fn do_scan(wifi: &'static WifiMutex, scan: &'static Mutex<Cs, ScanCache>) {
    {
        let mut cache = scan.lock().await;
        cache.status = ScanStatus::Pending;
        cache.aps.clear();
    }
    crate::sse_dirty::sse_mark(crate::core::constants::SSE_WIFI);
    let cfg = ScanConfig::default().with_max(24);
    let result = {
        let mut ctrl = wifi.lock().await;
        with_timeout(Duration::from_secs(12), ctrl.scan_async(&cfg)).await
    };
    let mut cache = scan.lock().await;
    match result {
        Ok(Ok(aps)) => {
            cache.aps.clear();
            for ap in aps.iter() {
                if ap.ssid.as_str().is_empty() {
                    continue;
                }
                let open = matches!(ap.auth_method, Some(AuthenticationMethod::None) | None);
                let mut row = ScanAp::default();
                let _ = row.ssid.push_str(ap.ssid.as_str());
                row.rssi = ap.signal_strength;
                row.open = open;
                if cache.aps.push(row).is_err() {
                    break;
                }
            }
            cache.status = ScanStatus::Ready;
            println!("WIFI scan: {} APs", cache.aps.len());
        }
        Ok(Err(e)) => {
            println!("WIFI scan err {e:?}");
            cache.status = ScanStatus::Failed;
        }
        Err(_) => {
            println!("WIFI scan timeout");
            cache.status = ScanStatus::Failed;
        }
    }
    crate::sse_dirty::sse_mark(crate::core::constants::SSE_WIFI);
}

async fn do_connect_test(
    wifi: &'static WifiMutex,
    ap_psk: &'static str,
    pending: &'static Mutex<Cs, WlanConfig>,
    test_state: &'static Mutex<Cs, heapless::String<12>>,
    test_ssid: &'static Mutex<Cs, heapless::String<33>>,
    test_owns: &'static Mutex<Cs, bool>,
) {
    let cfg = pending.lock().await.clone();
    {
        let mut st = test_state.lock().await;
        st.clear();
        let _ = st.push_str("testing");
        let mut ss = test_ssid.lock().await;
        ss.clear();
        let _ = ss.push_str(cfg.ssid.as_str());
        *test_owns.lock().await = true;
    }
    println!("WIFI test → {}", cfg.ssid.as_str());

    let ap_pass = ap_psk
        .try_into()
        .unwrap_or_else(|_| "Chaya2MQTTSetupKey!".try_into().expect("psk"));
    let ap_config = AccessPointConfig::default()
        .with_ssid(
            esp_radio::wifi::Ssid::try_from(SETUP_AP_SSID).expect("ap ssid"),
        )
        .with_authentication(AuthenticationMethodConfig::Wpa2Personal(ap_pass));

    let ssid = match esp_radio::wifi::Ssid::try_from(cfg.ssid.as_str()) {
        Ok(s) => s,
        Err(_) => {
            set_state(test_state, test_owns, "fail").await;
            return;
        }
    };
    let sta_auth = if cfg.pass.is_empty() {
        AuthenticationMethodConfig::Open
    } else {
        match cfg.pass.as_str().try_into() {
            Ok(p) => AuthenticationMethodConfig::Wpa2Personal(p),
            Err(_) => {
                set_state(test_state, test_owns, "fail").await;
                return;
            }
        }
    };
    let sta_config = StationConfig::default()
        .with_ssid(ssid)
        .with_authentication(sta_auth);
    let radio = Config::AccessPointStation(sta_config, ap_config);

    let ok = {
        let mut ctrl = wifi.lock().await;
        if ctrl.set_config(&radio).is_err() {
            false
        } else {
            match with_timeout(Duration::from_secs(15), ctrl.connect_async()).await {
                Ok(Ok(_)) => {
                    let _ = ctrl.disconnect_async().await;
                    true
                }
                Ok(Err(e)) => {
                    println!("WIFI test connect err {e:?}");
                    false
                }
                Err(_) => {
                    println!("WIFI test timeout");
                    let _ = ctrl.disconnect_async().await;
                    false
                }
            }
        }
    };

    // Restore SoftAP-only friendly APSTA with empty STA.
    let ap_pass = ap_psk
        .try_into()
        .unwrap_or_else(|_| "Chaya2MQTTSetupKey!".try_into().expect("psk"));
    let ap_config = AccessPointConfig::default()
        .with_ssid(
            esp_radio::wifi::Ssid::try_from(SETUP_AP_SSID).expect("ap ssid"),
        )
        .with_authentication(AuthenticationMethodConfig::Wpa2Personal(ap_pass));
    let restore = Config::AccessPointStation(StationConfig::default(), ap_config);
    {
        let mut ctrl = wifi.lock().await;
        let _ = ctrl.set_config(&restore);
    }

    set_state(test_state, test_owns, if ok { "ok" } else { "fail" }).await;
    Timer::after(Duration::from_millis(50)).await;
}

async fn set_state(
    test_state: &'static Mutex<Cs, heapless::String<12>>,
    test_owns: &'static Mutex<Cs, bool>,
    state: &str,
) {
    let mut st = test_state.lock().await;
    st.clear();
    let _ = st.push_str(state);
    *test_owns.lock().await = false;
}
