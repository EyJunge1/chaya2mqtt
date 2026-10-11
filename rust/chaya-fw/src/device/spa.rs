//! Embedded admin SPA (parity stub until Vite assets are flash-hosted).

pub const ADMIN_HTML: &str = include_str!("assets/admin.html");

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
    "/api/ota/check":{"post":{"summary":"Check GitHub latest"}}
  }
}"#;
