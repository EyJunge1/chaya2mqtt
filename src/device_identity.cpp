#include "device_identity.h"

#include "config/nvs_keys.h"
#include "config/nvs_utils.h"
#include "device_identity_pure.h"
#include "util/log_tag.h"

#include <cstdint>
#include <cstring>

#include <Preferences.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_random.h>

DEFINE_LOG_TAG("DEV_ID");

namespace {

char s_cachedId[kDeviceIdBufLen]{};
bool s_cached = false;

bool idFromStaMac(char* out, size_t outLen) {
    uint8_t mac[6] = {};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
        return false;
    }
    const uint8_t tail[3] = {mac[3], mac[4], mac[5]};
    return deviceIdFormatFromBytes(tail, out, outLen);
}

bool idFromRandom(char* out, size_t outLen) {
    uint8_t rnd[3] = {};
    esp_fill_random(rnd, sizeof(rnd));
    return deviceIdFormatFromBytes(rnd, out, outLen);
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

bool persistDeviceId(Preferences& prefs, const char* id) {
    if (!prefs.begin(kNvsNsCfg, false)) {
        return false;
    }
    const size_t written = prefs.putString(kNvsKeyCfgDeviceId, id);
    prefs.end();
    return written >= kDeviceIdHexLen;
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
            ESP_LOGW(TAG, "Invalid cfg/device_id in NVS — regenerating");
        } else {
            prefs.end();
        }
    }

    const bool priorSetup = hadPriorSetupConfig(prefs);
    const DeviceIdCreateMode mode = deviceIdCreateMode(priorSetup);

    char generated[kDeviceIdBufLen]{};
    if (mode == DeviceIdCreateMode::FromMacMigrate) {
        if (idFromStaMac(generated, sizeof(generated))) {
            ESP_LOGI(TAG, "Migrating device id from STA MAC: %s", generated);
        } else {
            ESP_LOGW(TAG, "MAC migrate failed — falling back to random id");
            if (!idFromRandom(generated, sizeof(generated))) {
                return;
            }
            ESP_LOGI(TAG, "Created random device id: %s", generated);
        }
    } else {
        if (!idFromRandom(generated, sizeof(generated))) {
            return;
        }
        ESP_LOGI(TAG, "Created random device id: %s", generated);
    }

    if (!persistDeviceId(prefs, generated)) {
        ESP_LOGW(TAG, "device_id NVS write failed; using RAM-only id %s until reboot", generated);
    }
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
