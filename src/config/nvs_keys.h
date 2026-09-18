#pragma once

/** Central NVS namespace and key names (see docs/CONFIGURATION.md). */
constexpr const char kNvsNsWifi[] = "wifi";
constexpr const char kNvsNsMqtt[] = "mqtt";
constexpr const char kNvsNsCfg[] = "cfg";
constexpr const char kNvsNsChaya[] = "chaya";

constexpr const char kNvsKeyWifiCfgV2[] = "cfg_v2";
constexpr const char kNvsKeyWifiApPin[] = "ap_pin";

constexpr const char kNvsKeyCfgDeviceId[] = "device_id";
constexpr const char kNvsKeyCfgRstPeriod[] = "rstPeriod";
constexpr const char kNvsKeyCfgUiLang[] = "ui_lang";
constexpr const char kNvsKeyCfgUiTheme[] = "ui_theme";
constexpr const char kNvsKeyCfgLedEn[] = "led_en";
constexpr const char kNvsKeyCfgDispView[] = "disp_view";
constexpr const char kNvsKeyCfgSndTxEn[] = "snd_tx_en";
constexpr const char kNvsKeyCfgSndRxEn[] = "snd_rx_en";
constexpr const char kNvsKeyCfgSndTxVol[] = "snd_tx_vol";
constexpr const char kNvsKeyCfgSndRxVol[] = "snd_rx_vol";
constexpr const char kNvsKeyCfgSndQuietBlob[] = "snd_qB";
constexpr const char kNvsKeyCfgSndToneBlob[] = "snd_tB";
constexpr const char kNvsKeyCfgUpdDay[] = "upd_day";
constexpr const char kNvsKeyCfgUpdChan[] = "upd_chan";

constexpr const char kNvsKeyChayaCounter[] = "counter";
constexpr const char kNvsKeyChayaSentCount[] = "sentCount";
constexpr const char kNvsKeyChayaBaselineBlob[] = "baseBlob";

/** Atomic MQTT broker+partner blob (PackedMqttConfigV1). */
constexpr const char kNvsKeyMqttCfgV1[] = "cfg_v1";
