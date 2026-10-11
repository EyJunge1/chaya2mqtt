//! Embedded admin SPA (Vite dist gzip blob when present, else admin.html fallback).

include!(concat!(env!("OUT_DIR"), "/spa_manifest.rs"));

pub const ADMIN_HTML: &str = SPA_INDEX_HTML;

pub const OPENAPI_JSON: &str = r#"{
  "openapi":"3.0.3",
  "info":{"title":"Chaya2MQTT","version":"0.1.0"},
  "paths":{
    "/api/status":{"get":{"summary":"Device status"}},
    "/api/events":{"get":{"summary":"SSE status stream"}},
    "/api/wifi":{"post":{"summary":"Save Wi-Fi (AP mode)"}},
    "/api/mqtt":{"post":{"summary":"Save MQTT (AP mode)"}},
    "/api/send":{"post":{"summary":"Bump heart TX"}},
    "/api/ota/status":{"get":{"summary":"OTA status"}},
    "/api/ota/check":{"post":{"summary":"Check GitHub latest"}},
    "/api/ota/install":{"post":{"summary":"Install latest (SHA-512)"}},
    "/api/mqtt":{"get":{"summary":"MQTT config snapshot"}},
    "/api/settings":{"get":{"summary":"Settings"},"post":{"summary":"Update settings"}},
    "/api/reboot":{"post":{"summary":"Software reboot"}},
    "/api/factory-reset":{"post":{"summary":"Wipe NVS + reboot"}}
  }
}"#;

pub fn find_asset(path: &str) -> Option<&'static SpaAsset> {
    SPA_ASSETS.iter().find(|a| a.path == path)
}

pub fn asset_bytes(asset: &SpaAsset) -> &'static [u8] {
    let end = asset.offset.saturating_add(asset.length).min(SPA_BLOB.len());
    &SPA_BLOB[asset.offset.min(SPA_BLOB.len())..end]
}
