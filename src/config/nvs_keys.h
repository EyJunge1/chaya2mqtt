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
constexpr const char kNvsKeyCfgUpdDay[]    = "upd_day";

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
