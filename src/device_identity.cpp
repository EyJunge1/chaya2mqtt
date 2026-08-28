#include "device_identity.h"

#include "config/nvs_keys.h"
#include "config/nvs_utils.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include <Preferences.h>
#include <esp_mac.h>
#include <esp_random.h>

namespace {

char s_cachedId[kDeviceIdBufLen]{};
bool s_cached = false;

bool formatIdFromBytes(const uint8_t bytes[3], char* out, size_t outLen) {
    if (out == nullptr || outLen < kDeviceIdBufLen) {
        return false;
    }
    const int n = std::snprintf(out, outLen, "%02x%02x%02x", bytes[0], bytes[1], bytes[2]);
    return n == static_cast<int>(kDeviceIdHexLen) && deviceIdSyntaxOk(out);
}

bool idFromStaMac(char* out, size_t outLen) {
    uint8_t mac[6] = {};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
        return false;
    }
    const uint8_t tail[3] = {mac[3], mac[4], mac[5]};
    return formatIdFromBytes(tail, out, outLen);
}

bool idFromRandom(char* out, size_t outLen) {
    uint8_t rnd[3] = {};
    esp_fill_random(rnd, sizeof(rnd));
    return formatIdFromBytes(rnd, out, outLen);
}

/** True if WiFi or MQTT NVS still has setup data (OTA upgrade path). */
bool hadPriorSetupConfig(Preferences& prefs) {
    if (prefs.begin(kNvsNsWifi, true)) {
        const bool wifi = prefs.isKey(kNvsKeyWifiCfgV2) || prefs.isKey(kNvsKeyWifiCredV1)
                          || prefs.isKey(kNvsKeyWifiSsid) || prefs.isKey(kNvsKeyWifiApPin);
        prefs.end();
        if (wifi) {
            return true;
        }
    }
    if (prefs.begin(kNvsNsMqtt, true)) {
        const bool mqtt = prefs.isKey(kNvsKeyMqttServer);
        prefs.end();
        return mqtt;
    }
    return false;
}

void ensureDeviceIdLocked(char* out, size_t outLen) {
    out[0] = '\0';

    Preferences prefs;
    if (prefs.begin(kNvsNsCfg, true)) {
        char stored[kDeviceIdBufLen]{};
        if (prefs.isKey(kNvsKeyCfgDeviceId)) {
            static_cast<void>(prefs.getString(kNvsKeyCfgDeviceId, stored, sizeof(stored)));
            prefs.end();
            if (deviceIdSyntaxOk(stored)) {
                std::memcpy(out, stored, kDeviceIdBufLen);
                return;
            }
            // Fall through: rewrite invalid / corrupt value.
        } else {
            prefs.end();
        }
    }

    char generated[kDeviceIdBufLen]{};
    const bool migrateFromMac = hadPriorSetupConfig(prefs) && idFromStaMac(generated, sizeof(generated));
    if (!migrateFromMac) {
        if (!idFromRandom(generated, sizeof(generated))) {
            return;
        }
    }

    if (!prefs.begin(kNvsNsCfg, false)) {
        // Still expose the ID this boot if NVS write fails.
        std::memcpy(out, generated, kDeviceIdBufLen);
        return;
    }
    static_cast<void>(prefs.putString(kNvsKeyCfgDeviceId, generated));
    prefs.end();
    std::memcpy(out, generated, kDeviceIdBufLen);
}

} // namespace

void buildDeviceId(char* out, size_t outLen) {
    if (out == nullptr || outLen < kDeviceIdBufLen) {
        if (out != nullptr && outLen > 0U) {
            out[0] = '\0';
        }
        return;
    }

    {
        app_nvs::ScopedNvsLock lock;
        if (!s_cached) {
            ensureDeviceIdLocked(s_cachedId, sizeof(s_cachedId));
            if (deviceIdSyntaxOk(s_cachedId)) {
                s_cached = true;
            }
        }
        if (s_cached) {
            std::memcpy(out, s_cachedId, kDeviceIdBufLen);
            return;
        }
    }

    out[0] = '\0';
}
