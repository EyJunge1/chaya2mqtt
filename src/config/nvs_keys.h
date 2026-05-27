#pragma once

/** Central NVS namespace and key names (see docs/CONFIGURATION.md). */
constexpr const char kNvsNsWifi[]  = "wifi";
constexpr const char kNvsNsMqtt[]  = "mqtt";
constexpr const char kNvsNsCfg[]   = "cfg";
constexpr const char kNvsNsChaya[] = "chaya";

constexpr const char kNvsKeyWifiCredV1[] = "cred_v1";
constexpr const char kNvsKeyWifiSsid[]   = "ssid";
constexpr const char kNvsKeyWifiPass[]   = "pass";

constexpr const char kNvsKeyCfgRstPeriod[] = "rstPeriod";
constexpr const char kNvsKeyCfgAuthEn[]    = "authEn";
constexpr const char kNvsKeyCfgUpdDay[]    = "upd_day";
