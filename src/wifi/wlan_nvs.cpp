#include "wlan.h"

#include "wlan_config.h"
#include "wlan_internal.h"
#include "wlan_pack.h"

#include "config/nvs_keys.h"
#include "config/nvs_utils.h"
#include "util/net_validate.h"

#include <Preferences.h>
#include <cstring>
#include <esp_log.h>

#include "util/log_tag.h"

DEFINE_LOG_TAG("WIFI");

bool wlanLoadConfigFromNvs(WlanConfig *cfg) {
    if (cfg == nullptr) {
        return false;
    }
    wlanConfigClear(cfg);

    app_nvs::ScopedNvsLock lock;
    Preferences prefs;
    if (!prefs.begin(kNvsNsWifi, true)) {
        return false;
    }

    bool loaded = false;
    if (prefs.getBytesLength(kNvsKeyWifiCfgV2) == sizeof(PackedWifiConfigV2)) {
        PackedWifiConfigV2 pk{};
        if (prefs.getBytes(kNvsKeyWifiCfgV2, &pk, sizeof(pk)) == sizeof(pk)) {
            loaded = wlanUnpackConfigV2(pk, cfg);
            if (loaded) {
                ESP_LOGD(TAG, "WiFi NVS: cfg_v2 loaded (ssid=%s, mode=%s)", cfg->ssid,
                         cfg->mode == WlanIpMode::Static ? "static" : "dhcp");
            } else {
                ESP_LOGW(TAG, "WiFi NVS: cfg_v2 rejected");
            }
        }
    }

    if (!loaded && prefs.getBytesLength(kNvsKeyWifiCredV1) == sizeof(PackedWifiCredentials)) {
        PackedWifiCredentials pk{};
        if (prefs.getBytes(kNvsKeyWifiCredV1, &pk, sizeof(pk)) == sizeof(pk) && pk.magic == kWifiCredPackedMagic) {
            pk.ssid[sizeof(pk.ssid) - 1U] = '\0';
            pk.pass[sizeof(pk.pass) - 1U] = '\0';
            if (pk.ssid[0] != '\0' && strnlen(pk.ssid, sizeof(pk.ssid)) < sizeof(pk.ssid) &&
                strnlen(pk.pass, sizeof(pk.pass)) < sizeof(pk.pass)) {
                wlanConfigClear(cfg);
                strlcpy(cfg->ssid, pk.ssid, sizeof(cfg->ssid));
                strlcpy(cfg->pass, pk.pass, sizeof(cfg->pass));
                cfg->mode = WlanIpMode::Dhcp;
                loaded = true;
                ESP_LOGD(TAG, "WiFi NVS: migrated cred_v1 → DHCP (ssid=%s)", cfg->ssid);
            }
        }
    }

    if (!loaded) {
        char ssid[kWifiSsidMaxLen]{};
        char pass[kWifiPassMaxLen]{};
        prefs.getString(kNvsKeyWifiSsid, ssid, sizeof(ssid));
        prefs.getString(kNvsKeyWifiPass, pass, sizeof(pass));
        ssid[sizeof(ssid) - 1U] = '\0';
        pass[sizeof(pass) - 1U] = '\0';
        if (ssid[0] != '\0') {
            wlanConfigClear(cfg);
            strlcpy(cfg->ssid, ssid, sizeof(cfg->ssid));
            strlcpy(cfg->pass, pass, sizeof(cfg->pass));
            cfg->mode = WlanIpMode::Dhcp;
            loaded = true;
            ESP_LOGD(TAG, "WiFi NVS: migrated legacy ssid/pass → DHCP (ssid=%s)", cfg->ssid);
        }
    }
    prefs.end();

    if (!loaded) {
        ESP_LOGD(TAG, "WiFi NVS: no SSID stored");
    }
    return loaded;
}

void wifiLoadCredentialsFromNvs(char *ssid, size_t ssidLen, char *pass, size_t passLen) {
    if (ssid == nullptr || pass == nullptr || ssidLen == 0U || passLen == 0U) {
        return;
    }
    ssid[0] = '\0';
    pass[0] = '\0';
    WlanConfig cfg{};
    if (!wlanLoadConfigFromNvs(&cfg)) {
        return;
    }
    strlcpy(ssid, cfg.ssid, ssidLen);
    strlcpy(pass, cfg.pass, passLen);
}

bool wlanSaveConfigToNvs(const WlanConfig &cfg) {
    if (wlanConfigValidate(&cfg) != nullptr) {
        ESP_LOGE(TAG, "NVS wifi: refuse save (invalid config)");
        return false;
    }

    PackedWifiConfigV2 pk{};
    wlanPackConfigV2(cfg, &pk);

    app_nvs::ScopedNvsLock lock;
    Preferences prefs;
    if (!prefs.begin(kNvsNsWifi, false)) {
        ESP_LOGE(TAG, "NVS wifi: begin(write) failed");
        return false;
    }
    prefs.remove(kNvsKeyWifiSsid);
    prefs.remove(kNvsKeyWifiPass);
    prefs.remove(kNvsKeyWifiCredV1);
    const size_t w = prefs.putBytes(kNvsKeyWifiCfgV2, &pk, sizeof(pk));
    prefs.end();
    if (w != sizeof(pk)) {
        ESP_LOGE(TAG, "NVS wifi: cfg_v2 write failed");
        return false;
    }
    ESP_LOGI(TAG, "WiFi NVS saved ssid=%s mode=%s", cfg.ssid, cfg.mode == WlanIpMode::Static ? "static" : "dhcp");
    return true;
}

bool configSaveWiFiCredentials(const char *ssid, const char *password) {
    if (ssid == nullptr || ssid[0] == '\0') {
        return false;
    }
    WlanConfig cfg{};
    wlanConfigClear(&cfg);
    strlcpy(cfg.ssid, ssid, sizeof(cfg.ssid));
    strlcpy(cfg.pass, password != nullptr ? password : "", sizeof(cfg.pass));
    cfg.mode = WlanIpMode::Dhcp;
    return wlanSaveConfigToNvs(cfg);
}
