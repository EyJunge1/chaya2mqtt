#pragma once

#include <cstring>
#include <string>

#include "mqtt/config.h"
#include "mqtt/pairing.h"
#include "wifi/wlan_config.h"
#include "wifi/wlan_pack.h"

/** In-memory NVS stand-in for the device simulator. */
class FakeNvs {
  public:
    mutable bool failNextWifiSave = false;
    mutable bool failNextWifiLoad = false;
    mutable bool failNextMqttSave = false;
    mutable bool failNextMqttLoad = false;

    void clear() {
        wifiPacked_.clear();
        hasWifi_ = false;
        mqtt_    = MqttConfig{};
        hasMqtt_ = false;
        failNextWifiSave = failNextWifiLoad = failNextMqttSave = failNextMqttLoad = false;
    }

    bool saveWifi(const WlanConfig& cfg) {
        if (failNextWifiSave) {
            failNextWifiSave = false;
            return false;
        }
        if (wlanConfigValidate(&cfg) != nullptr) {
            return false;
        }
        PackedWifiConfigV2 pk{};
        wlanPackConfigV2(cfg, &pk);
        wifiPacked_.assign(reinterpret_cast<const char*>(&pk), sizeof(pk));
        hasWifi_ = true;
        return true;
    }

    bool loadWifi(WlanConfig* cfg) const {
        if (failNextWifiLoad) {
            failNextWifiLoad = false;
            return false;
        }
        if (!hasWifi_ || cfg == nullptr || wifiPacked_.size() != sizeof(PackedWifiConfigV2)) {
            return false;
        }
        PackedWifiConfigV2 pk{};
        std::memcpy(&pk, wifiPacked_.data(), sizeof(pk));
        return wlanUnpackConfigV2(pk, cfg);
    }

    bool saveMqtt(const MqttConfig& cfg, const char* ownId) {
        if (failNextMqttSave) {
            failNextMqttSave = false;
            return false;
        }
        MqttConfig copy = cfg;
        mqttSanitizeConfigAfterLoad(copy, ownId);
        mqtt_    = copy;
        hasMqtt_ = true;
        return true;
    }

    bool loadMqtt(MqttConfig* cfg, const char* ownId) const {
        if (failNextMqttLoad) {
            failNextMqttLoad = false;
            return false;
        }
        if (!hasMqtt_ || cfg == nullptr) {
            return false;
        }
        *cfg = mqtt_;
        mqttSanitizeConfigAfterLoad(*cfg, ownId);
        return true;
    }

  private:
    std::string wifiPacked_;
    bool        hasWifi_ = false;
    MqttConfig  mqtt_{};
    bool        hasMqtt_ = false;
};
