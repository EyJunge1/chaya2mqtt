#pragma once

/** Central NVS namespace and key names (see docs/CONFIGURATION.md). */
constexpr const char kNvsNsWifi[]  = "wifi";
constexpr const char kNvsNsMqtt[]  = "mqtt";
constexpr const char kNvsNsCfg[]   = "cfg";
constexpr const char kNvsNsChaya[] = "chaya";

constexpr const char kNvsKeyWifiCredV1[] = "cred_v1";
constexpr const char kNvsKeyWifiCfgV2[]  = "cfg_v2";
constexpr const char kNvsKeyWifiSsid[]   = "ssid";
constexpr const char kNvsKeyWifiPass[]   = "pass";
constexpr const char kNvsKeyWifiApPin[]  = "ap_pin";

constexpr const char kNvsKeyCfgRstPeriod[] = "rstPeriod";
constexpr const char kNvsKeyCfgUiLang[]    = "ui_lang";
constexpr const char kNvsKeyCfgUiTheme[]   = "ui_theme";
constexpr const char kNvsKeyCfgLedEn[]     = "led_en";
constexpr const char kNvsKeyCfgSndMute[]   = "snd_mute";
constexpr const char kNvsKeyCfgSndVol[]    = "snd_vol";
constexpr const char kNvsKeyCfgSndQ0[]     = "snd_q0";
constexpr const char kNvsKeyCfgSndQ1[]     = "snd_q1";
constexpr const char kNvsKeyCfgSndCustom[] = "snd_custom";
constexpr const char kNvsKeyCfgSndTxHz[]   = "snd_tx_hz";
constexpr const char kNvsKeyCfgSndTxMs[]   = "snd_tx_ms";
constexpr const char kNvsKeyCfgSndRxHz[]   = "snd_rx_hz";
constexpr const char kNvsKeyCfgSndRxMs[]   = "snd_rx_ms";
constexpr const char kNvsKeyCfgUpdDay[]    = "upd_day";
constexpr const char kNvsKeyCfgUpdChan[]   = "upd_chan";

constexpr const char kNvsKeyChayaCounter[]     = "counter";
constexpr const char kNvsKeyChayaSentCount[]   = "sentCount";
constexpr const char kNvsKeyChayaCntBase[]     = "cntBase";
constexpr const char kNvsKeyChayaSntBase[]     = "sntBase";
constexpr const char kNvsKeyChayaRstDay[]      = "rstDay";
constexpr const char kNvsKeyChayaBaselineBlob[] = "baseBlob";

constexpr const char kNvsKeyMqttServer[]     = "server";
constexpr const char kNvsKeyMqttPort[]       = "port";
constexpr const char kNvsKeyMqttUser[]       = "user";
constexpr const char kNvsKeyMqttPass[]       = "pass";
constexpr const char kNvsKeyMqttTopicPub[]  = "topic_pub";
constexpr const char kNvsKeyMqttTopicSub[]  = "topic_sub";
constexpr const char kNvsKeyMqttPartnerId[] = "partner_id";
