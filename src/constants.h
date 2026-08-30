#pragma once

#include "audio/audio_config.h"
#include "util/net_validate.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>

/** Minimum plausible Unix time (UTC) after NTP sync — rejects unset RTC (~1970). */
constexpr uint32_t kNtpMinValidUtcEpoch = 1700000000U;

/** Device pairing: 6 lowercase hex chars derived from MAC (last 3 bytes). */
constexpr size_t kDeviceIdHexLen = 6U;
constexpr size_t kDeviceIdBufLen = 7U;

/** Setup-AP hostname (no dots); STA uses kDeviceStaHostnamePrefix + device ID. */
constexpr const char kDeviceHostname[] = "chaya2mqtt";
/** Prefix for the unique station / mDNS hostname. */
constexpr const char kDeviceStaHostnamePrefix[] = "chaya2mqtt-";
constexpr size_t kDeviceStaHostnameBufLen =
    sizeof(kDeviceStaHostnamePrefix) - 1U + kDeviceIdHexLen + 1U;
/** Captive-portal AP SSID during setup. */
constexpr const char kSetupApSsid[] = "Chaya2MQTT";
/** SoftAP IPv4 shown on splash / captive clients. */
constexpr const char kSetupApIp[] = "4.3.2.1";
/** Absolute captive-portal landing URL (Android/Windows prefer absolute). */
constexpr const char kSetupApCaptiveRedirect[] = "http://4.3.2.1/";

/** Setup-AP origin; the station origin is derived from the device ID. */
constexpr const char kDeviceHttpOrigin[] = "http://chaya2mqtt.local/";

inline bool ntpTimeLooksSynced(time_t utcNow) {
    return utcNow > static_cast<time_t>(kNtpMinValidUtcEpoch);
}

/**
 * Basic MQTT topic rules: non-empty, within maxLen (including NUL), no spaces or wildcards.
 */
inline bool mqttTopicSyntaxOk(const char* topic, size_t maxLen) {
    if (topic == nullptr || topic[0] == '\0' || maxLen == 0U) {
        return false;
    }
    size_t len = 0;
    for (const char* p = topic; *p != '\0'; ++p) {
        ++len;
        if (len >= maxLen) {
            return false;
        }
        const unsigned char c = static_cast<unsigned char>(*p);
        if (c < 0x20U || c > 0x7EU || c == ' ' || c == '#' || c == '+') {
            return false;
        }
    }
    return true;
}

/**
 * MQTT broker host field (hostname or IP literal): fits in buffer with NUL, no control chars/spaces/wildcards.
 * Not the same rules as MQTT topics (still rejects '#' / '+').
 */
inline bool mqttServerSyntaxOk(const char* host, size_t maxLen) {
    return hostFieldSyntaxOk(host, maxLen);
}

/**
 * MQTT username: empty OK (anonymous); otherwise printable ASCII (0x20–0x7E), fits in maxLen.
 * SEC-09.
 */
inline bool mqttUsernameSyntaxOk(const char* user, size_t maxLen) {
    if (user == nullptr || maxLen == 0U) {
        return false;
    }
    if (user[0] == '\0') {
        return true;
    }
    size_t len = 0;
    for (const char* p = user; *p != '\0'; ++p) {
        ++len;
        if (len >= maxLen) {
            return false;
        }
        const unsigned char c = static_cast<unsigned char>(*p);
        if (c < 0x20U || c > 0x7EU) {
            return false;
        }
    }
    return true;
}

/**
 * MQTT password: empty OK; no control chars (< 0x20); fits in maxLen.
 * Allows high-bit / punctuation for strong secrets; blocks embedded-NUL truncation class. SEC-09.
 */
inline bool mqttPasswordSyntaxOk(const char* pass, size_t maxLen) {
    if (pass == nullptr || maxLen == 0U) {
        return false;
    }
    if (pass[0] == '\0') {
        return true;
    }
    size_t len = 0;
    for (const char* p = pass; *p != '\0'; ++p) {
        ++len;
        if (len >= maxLen) {
            return false;
        }
        if (static_cast<unsigned char>(*p) < 0x20U) {
            return false;
        }
    }
    return true;
}

/** UI language preference: "de" or "en". */
inline bool uiLangSyntaxOk(const char* lang) {
    return lang != nullptr && (strcmp(lang, "de") == 0 || strcmp(lang, "en") == 0);
}

/** UI theme preference: "dark" or "light". */
inline bool uiThemeSyntaxOk(const char* theme) {
    return theme != nullptr && (strcmp(theme, "dark") == 0 || strcmp(theme, "light") == 0);
}

/** Boolean form value: "0"/"1" or "true"/"false". */
inline bool formBoolSyntaxOk(const char* value) {
    return value != nullptr
           && (strcmp(value, "0") == 0 || strcmp(value, "1") == 0 || strcmp(value, "true") == 0
               || strcmp(value, "false") == 0);
}

/** Parse a validated boolean form value (call only after formBoolSyntaxOk). */
inline bool formBoolFromForm(const char* value) {
    return value != nullptr && (strcmp(value, "1") == 0 || strcmp(value, "true") == 0);
}

inline bool audioVolumeInRange(int v) {
    return v >= 0 && v <= static_cast<int>(kAudioVolumeMax);
}

inline bool quietHourInRange(int v) {
    return v >= 0 && v <= static_cast<int>(kAudioHourMax);
}

inline bool audioToneHzInRange(int v) {
    return v >= static_cast<int>(kAudioToneHzMin) && v <= static_cast<int>(kAudioToneHzMax);
}

inline bool audioToneMsInRange(int v) {
    return v >= static_cast<int>(kAudioToneMsMin) && v <= static_cast<int>(kAudioToneMsMax);
}

/** Display reset period days: 0 = off, otherwise 1–30. */
inline bool resetPeriodDaysInRange(int v) {
    return v >= 0 && v <= 30;
}

/** Six lowercase hex digits (a-f0-9), e.g. a1b2c3. */
inline bool deviceIdSyntaxOk(const char* id) {
    if (id == nullptr) {
        return false;
    }
    for (size_t i = 0; i < kDeviceIdHexLen; ++i) {
        const unsigned char c = static_cast<unsigned char>(id[i]);
        if (c == '\0') {
            return false;
        }
        const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        if (!hex) {
            return false;
        }
    }
    return id[kDeviceIdHexLen] == '\0';
}

inline bool wifiSsidSyntaxOk(const char* ssid, size_t maxLen) {
    if (ssid == nullptr || ssid[0] == '\0' || maxLen == 0U) {
        return false;
    }
    size_t len = 0;
    for (const char* p = ssid; *p != '\0'; ++p) {
        ++len;
        if (len >= maxLen) {
            return false;
        }
        const unsigned char c = static_cast<unsigned char>(*p);
        if (c < 0x20U || c > 0x7EU) {
            return false;
        }
    }
    return true;
}
