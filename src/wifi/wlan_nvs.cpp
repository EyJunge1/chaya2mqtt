#include "wlan.h"

#include "wlan_config.h"
#include "wlan_internal.h"

#include "config/nvs_keys.h"
#include "config/nvs_utils.h"
#include "mqtt/config.h"

#include <Preferences.h>
#include <cstring>
#include <esp_log.h>

#include "log_tag.h"

DEFINE_LOG_TAG("WIFI");

struct PackedWifiCredentials {
    uint32_t magic;
    char     ssid[kWifiSsidMaxLen];
    char     pass[kWifiPassMaxLen];
};

void wifiLoadCredentialsFromNvs(char* ssid, size_t ssidLen, char* pass, size_t passLen) {
    if (ssid == nullptr || pass == nullptr || ssidLen == 0U || passLen == 0U) {
        return;
    }
    ssid[0] = '\0';
    pass[0] = '\0';
    app_nvs::ScopedNvsLock lock;
    Preferences prefs;
    if (!prefs.begin(kNvsNsWifi, true)) {
        return;
    }

    bool loadedFromPacked = false;
    if (prefs.getBytesLength(kNvsKeyWifiCredV1) == sizeof(PackedWifiCredentials)) {
        PackedWifiCredentials pk{};
        if (prefs.getBytes(kNvsKeyWifiCredV1, &pk, sizeof(pk)) == sizeof(pk)
            && pk.magic == kWifiCredPackedMagic) {
            pk.ssid[sizeof(pk.ssid) - 1U] = '\0';
            pk.pass[sizeof(pk.pass) - 1U] = '\0';
            if (pk.ssid[0] != '\0' && strnlen(pk.ssid, sizeof(pk.ssid)) < sizeof(pk.ssid)
                && strnlen(pk.pass, sizeof(pk.pass)) < sizeof(pk.pass)) {
                strlcpy(ssid, pk.ssid, ssidLen);
                strlcpy(pass, pk.pass, passLen);
                loadedFromPacked = true;
            } else {
                ESP_LOGW(TAG, "WiFi NVS: cred_v1 rejected (missing NUL or empty SSID)");
            }
        }
    }
    if (!loadedFromPacked) {
        prefs.getString(kNvsKeyWifiSsid, ssid, ssidLen);
        prefs.getString(kNvsKeyWifiPass, pass, passLen);
        ssid[ssidLen - 1U] = '\0';
        pass[passLen - 1U]  = '\0';
    }
    prefs.end();

    if (ssid[0] != '\0') {
        ESP_LOGD(TAG, "WiFi NVS: credentials loaded (ssid=%s, packed=%s)", ssid,
                 loadedFromPacked ? "yes" : "no");
    } else {
        ESP_LOGD(TAG, "WiFi NVS: no SSID stored");
    }
}

bool configSaveWiFiCredentials(const char* ssid, const char* password) {
    if (ssid == nullptr || ssid[0] == '\0') {
        return false;
    }
    PackedWifiCredentials pk{};
    pk.magic = kWifiCredPackedMagic;
    strlcpy(pk.ssid, ssid, sizeof(pk.ssid));
    strlcpy(pk.pass, password != nullptr ? password : "", sizeof(pk.pass));

    app_nvs::ScopedNvsLock lock;
    Preferences prefs;
    if (!prefs.begin(kNvsNsWifi, false)) {
        ESP_LOGE(TAG, "NVS wifi: begin(write) failed");
        return false;
    }
    prefs.remove(kNvsKeyWifiSsid);
    prefs.remove(kNvsKeyWifiPass);
    const size_t w = prefs.putBytes(kNvsKeyWifiCredV1, &pk, sizeof(pk));
    prefs.end();
    if (w != sizeof(pk)) {
        ESP_LOGE(TAG, "NVS wifi: credential blob write failed");
        return false;
    }
    return true;
}
