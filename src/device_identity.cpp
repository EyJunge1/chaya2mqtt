#include "device_identity.h"

#include <cstdint>
#include <cstdio>

#include <esp_mac.h>

void buildDeviceId(char* out, size_t outLen) {
    if (out == nullptr || outLen < kDeviceIdBufLen) {
        if (out != nullptr && outLen > 0U) {
            out[0] = '\0';
        }
        return;
    }
    uint8_t mac[6] = {};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
        out[0] = '\0';
        return;
    }
    static_cast<void>(std::snprintf(out, outLen, "%02x%02x%02x", mac[3], mac[4], mac[5]));
}
