//! Embedded admin SPA (Vite dist gzip blob when present, else admin.html fallback).

include!(concat!(env!("OUT_DIR"), "/spa_manifest.rs"));

pub const ADMIN_HTML: &str = SPA_INDEX_HTML;

/// Compact path index for `/openapi.json` (full contract: `docs/api/openapi.yaml`).
pub const OPENAPI_JSON: &str = r#"{
  "openapi":"3.0.3",
  "info":{"title":"Chaya2MQTT","version":"dev","description":"Compact device index; canonical OpenAPI is docs/api/openapi.yaml"},
  "paths":{
    "/api/bootstrap":{"get":{"summary":"Bootstrap payload"}},
    "/api/chaya/send":{"post":{"summary":"Bump heart TX"}},
    "/api/wifi/config":{"get":{"summary":"Wi-Fi config"}},
    "/api/wifi/scan":{"get":{"summary":"Scan networks"},"post":{"summary":"Trigger scan"}},
    "/api/wifi/connect":{"post":{"summary":"Connect / save Wi-Fi"}},
    "/api/wifi/connect-status":{"get":{"summary":"Connect-test status"}},
    "/api/wifi/connect-commit":{"post":{"summary":"Commit connect-test (AP)"}},
    "/api/wifi/connect-abort":{"post":{"summary":"Abort connect-test (AP)"}},
    "/api/wifi/connect-retry":{"post":{"summary":"Retry connect-test (AP)"}},
    "/api/mqtt":{"get":{"summary":"MQTT config"},"post":{"summary":"Save MQTT"}},
    "/api/settings":{"get":{"summary":"Settings"},"post":{"summary":"Update settings"}},
    "/api/reboot":{"post":{"summary":"Software reboot"}},
    "/api/factory-reset":{"post":{"summary":"Wipe FlashKv + reboot"}},
    "/api/update/status":{"get":{"summary":"OTA status"}},
    "/api/update/check":{"post":{"summary":"Check GitHub latest"}},
    "/api/update/install":{"post":{"summary":"Install latest (SHA-512)"}},
    "/api/status":{"get":{"summary":"Device status (legacy)"}},
    "/api/events":{"get":{"summary":"SSE status stream"}},
    "/events":{"get":{"summary":"SSE status stream"}},
    "/api/send":{"post":{"summary":"Bump heart TX (alias)"}},
    "/api/ota/status":{"get":{"summary":"OTA status (alias)"}},
    "/api/ota/check":{"post":{"summary":"Check GitHub latest (alias)"}},
    "/api/ota/install":{"post":{"summary":"Install latest (alias)"}}
  }
}"#;

pub fn find_asset(path: &str) -> Option<&'static SpaAsset> {
    SPA_ASSETS.iter().find(|a| a.path == path)
}

pub fn asset_bytes(asset: &SpaAsset) -> &'static [u8] {
    let end = asset
        .offset
        .saturating_add(asset.length)
        .min(SPA_BLOB.len());
    &SPA_BLOB[asset.offset.min(SPA_BLOB.len())..end]
}
