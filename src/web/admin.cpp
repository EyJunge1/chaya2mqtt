#include <Arduino.h>

#include "admin.h"

#include "admin_globals.h"
#include "admin_routes.h"

#include "config/app_config.h"
#include "heart/counter.h"
#include "display/display.h"
#include "mqtt/mqtt.h"
#include "mqtt/config.h"
#include "ota/ota.h"
#include "wifi/wlan.h"
#include "auth.h"

#include <ESPAsyncWebServer.h>
#include <climits>

AsyncWebServer& webAdminWebServer() {
    static AsyncWebServer server(80);
    return server;
}

namespace {
bool g_webAdminRoutesRegistered = false;
} // namespace

void webAdminRegisterRoutes() {
    if (g_webAdminRoutesRegistered) {
        return;
    }
    g_webAdminRoutesRegistered = true;

    webAuthInit();
    AsyncWebServer& ws = webAdminWebServer();
    webAuthRegisterRoutes(ws);

    adminRoutesRegisterWifi(ws);
    adminRoutesRegisterMqtt(ws);
    adminRoutesRegisterApplication(ws);
}

void webAdminScheduleWifiConfiguredReboot() {
    g_webAdminWifiReconnectRequested.store(true, std::memory_order_release);
}

void webAdminLoop() {
    webAuthLoop();

    if (g_webAdminSettingsApplyPending.exchange(false, std::memory_order_acq_rel)) {
        uint8_t daysApply;
        bool    authApply = false;
        portENTER_CRITICAL(&g_webAdminSettingsPendingMux);
        daysApply = g_webAdminPendingResetDays;
        authApply = g_webAdminPendingAuthEnabled;
        portEXIT_CRITICAL(&g_webAdminSettingsPendingMux);
        configSetResetPeriodDays(daysApply);
        configSetWebAuthEnabled(authApply);
    }

    if (g_webAdminMqttApplyPending.exchange(false, std::memory_order_acq_rel)) {
        mqttCfgApplyPendingToActive();
        saveMQTTConfig();
        mqttDisconnect();
        mqttSetup();
        mqttPostponeConnect(3000UL);
        requestHeartRedraw();
    }

    if (g_webAdminChayaSendRequested.exchange(false, std::memory_order_acq_rel)) {
        if (mqttPublishChaya()) {
            if (heartSentCounter < INT_MAX) {
                ++heartSentCounter;
            }
            maybeSaveHeartSentCounter();
            requestHeartRedraw();
        }
    }

    if (g_webAdminRebootRequested.exchange(false, std::memory_order_acq_rel)
        || g_webAdminWifiReconnectRequested.exchange(false, std::memory_order_acq_rel)) {
        flushHeartCounterIfDirty();
        flushHeartSentCounterIfDirty();
        delay(200);
        releaseGpioHoldBeforeRestart();
        ESP.restart();
    }

    otaLoop();
}
