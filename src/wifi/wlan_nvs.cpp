#include "wlan.h"

#include "wlan_config.h"
#include "wlan_internal.h"
#include "wlan_pack.h"

#include "config/nvs_blob_load_pure.h"
#include "config/nvs_keys.h"
#include "config/nvs_utils.h"

#include <Preferences.h>
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
                ESP_LOGW(TAG, "WiFi NVS: cfg_v2 rejected");
            }
        }
        break;
    }
    case NvsBlobLoad::UseDefaults:
        if (v2Len != 0U) {
            ESP_LOGW(TAG, "WiFi NVS: cfg_v2 present but invalid size");
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
