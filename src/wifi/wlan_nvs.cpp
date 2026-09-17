#include "wlan.h"

#include "wlan_config.h"
#include "wlan_internal.h"
#include "wlan_pack.h"

#include "config/nvs_blob_load_pure.h"
#include "config/nvs_keys.h"
#include "config/nvs_utils.h"
#include "util/net_validate.h"

#include <Preferences.h>
#include <cstring>
#include <esp_log.h>

#include "util/log_tag.h"

DEFINE_LOG_TAG("WIFI");

// BUG-WEB-05: last successful NVS snapshot for GET without g_nvsMutex (not s_activeWlanConfig).
static WlanConfig s_nvsWlanConfigCache{};
static bool s_nvsWlanConfigCacheValid = false;
static portMUX_TYPE s_nvsWlanConfigCacheMux = portMUX_INITIALIZER_UNLOCKED;

static void wlanCacheNvsConfig(const WlanConfig &cfg) {
    portENTER_CRITICAL(&s_nvsWlanConfigCacheMux);
    s_nvsWlanConfigCache = cfg;
    s_nvsWlanConfigCacheValid = true;
    portEXIT_CRITICAL(&s_nvsWlanConfigCacheMux);
}

bool wlanCopyCachedConfig(WlanConfig *out) {
    if (out == nullptr) {
        return false;
    }
    portENTER_CRITICAL(&s_nvsWlanConfigCacheMux);
    if (!s_nvsWlanConfigCacheValid) {
        portEXIT_CRITICAL(&s_nvsWlanConfigCacheMux);
        return false;
    }
    *out = s_nvsWlanConfigCache;
    portEXIT_CRITICAL(&s_nvsWlanConfigCacheMux);
    return true;
}

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
    const size_t v2Len = prefs.getBytesLength(kNvsKeyWifiCfgV2);
    switch (nvsBlobLoadDecide(v2Len, sizeof(PackedWifiConfigV2))) {
    case NvsBlobLoad::UseBlob: {
        PackedWifiConfigV2 pk{};
        if (prefs.getBytes(kNvsKeyWifiCfgV2, &pk, sizeof(pk)) == sizeof(pk)) {
            loaded = wlanUnpackConfigV2(pk, cfg);
            if (loaded) {
                ESP_LOGD(TAG, "WiFi NVS: cfg_v2 loaded (ssid=%s, mode=%s)", cfg->ssid,
                         cfg->mode == WlanIpMode::Static ? "static" : "dhcp");
            } else {
                ESP_LOGW(TAG, "WiFi NVS: cfg_v2 rejected — ignoring legacy keys");
            }
        }
        break;
    }
    case NvsBlobLoad::UseDefaults:
        ESP_LOGW(TAG, "WiFi NVS: cfg_v2 present but invalid size — ignoring legacy keys");
        break;
    case NvsBlobLoad::UseLegacy:
        if (prefs.getBytesLength(kNvsKeyWifiCredV1) == sizeof(PackedWifiCredentials)) {
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
        break;
    }
    prefs.end();

    if (!loaded) {
        ESP_LOGD(TAG, "WiFi NVS: no SSID stored");
        return false;
    }
    wlanCacheNvsConfig(*cfg);
    return true;
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
    if (app_nvs::writesBlocked(kNvsNsWifi)) {
        ESP_LOGW(TAG, "NVS wifi: save blocked during shutdown");
        return false;
    }
    if (wlanConfigValidate(&cfg) != nullptr) {
        ESP_LOGE(TAG, "NVS wifi: refuse save (invalid config)");
        return false;
    }

    PackedWifiConfigV2 pk{};
    wlanPackConfigV2(cfg, &pk);

    app_nvs::ScopedNvsWriteLock lock(kNvsNsWifi);
    if (!lock) {
        ESP_LOGW(TAG, "NVS wifi: save blocked during shutdown");
        return false;
    }
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
    wlanCacheNvsConfig(cfg);
    return true;
}

void wlanResetRamAfterFactoryClear() {
    portENTER_CRITICAL(&s_nvsWlanConfigCacheMux);
    s_nvsWlanConfigCacheValid = false;
    wlanConfigClear(&s_nvsWlanConfigCache);
    portEXIT_CRITICAL(&s_nvsWlanConfigCacheMux);
    wlanConfigClear(&s_activeWlanConfig);
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
