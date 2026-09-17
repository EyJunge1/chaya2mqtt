#include <cstring>
#include <unity.h>

#include "async/system_shutdown_pure.h"
#include "constants.h"
#include "web/admin_restart_pure.h"
#include "web/host_validate.h"
#include "web/spa_asset_lookup.h"
#include "web/sse_dirty_pure.h"
#include "web/sse_send_pure.h"
#include "web/wifi_status_cache_pure.h"

void test_ui_pref_syntax() {
    TEST_ASSERT_TRUE(uiLangSyntaxOk("de"));
    TEST_ASSERT_TRUE(uiLangSyntaxOk("en"));
    TEST_ASSERT_FALSE(uiLangSyntaxOk("fr"));
    TEST_ASSERT_FALSE(uiLangSyntaxOk(nullptr));
    TEST_ASSERT_TRUE(uiThemeSyntaxOk("dark"));
    TEST_ASSERT_TRUE(uiThemeSyntaxOk("light"));
    TEST_ASSERT_TRUE(uiThemeSyntaxOk("system"));
    TEST_ASSERT_FALSE(uiThemeSyntaxOk("auto"));
    TEST_ASSERT_FALSE(uiThemeSyntaxOk(nullptr));
}

void test_settings_range_helpers() {
    TEST_ASSERT_TRUE(audioVolumeInRange(0));
    TEST_ASSERT_TRUE(audioVolumeInRange(100));
    TEST_ASSERT_FALSE(audioVolumeInRange(101));
    TEST_ASSERT_TRUE(quietHourInRange(23));
    TEST_ASSERT_FALSE(quietHourInRange(24));
}

void test_host_validate() {
    constexpr const char *hostname = "chaya2mqtt-a1b2c3";
    TEST_ASSERT_FALSE(webHostCStringAllowed("", false, hostname, nullptr));
    TEST_ASSERT_TRUE(webHostCStringAllowed("", true, kDeviceHostname, nullptr));
    TEST_ASSERT_TRUE(webHostCStringAllowed("chaya2mqtt-a1b2c3", false, hostname, nullptr));
    TEST_ASSERT_TRUE(webHostCStringAllowed("chaya2mqtt-a1b2c3:80", false, hostname, nullptr));
    TEST_ASSERT_TRUE(webHostCStringAllowed("Chaya2MQTT-a1b2c3.local", false, hostname, nullptr));
    TEST_ASSERT_TRUE(webHostCStringAllowed("chaya2mqtt-a1b2c3.local:80", false, hostname, nullptr));
    TEST_ASSERT_TRUE(webHostCStringAllowed("4.3.2.1", true, kDeviceHostname, nullptr));
    TEST_ASSERT_TRUE(webHostCStringAllowed("chaya2mqtt", true, kDeviceHostname, nullptr));
    TEST_ASSERT_TRUE(webHostCStringAllowed("chaya2mqtt:80", true, kDeviceHostname, nullptr));
    TEST_ASSERT_TRUE(webHostCStringAllowed("chaya2mqtt.local", true, kDeviceHostname, nullptr));
    TEST_ASSERT_FALSE(webHostCStringAllowed("captive.apple.com", true, kDeviceHostname, nullptr));
    TEST_ASSERT_FALSE(webHostCStringAllowed("evil.example", true, kDeviceHostname, nullptr));
    TEST_ASSERT_FALSE(webHostCStringAllowed("chaya2mqtt.local", false, hostname, nullptr));
    TEST_ASSERT_FALSE(webHostCStringAllowed("evil.example", false, hostname, nullptr));
    TEST_ASSERT_TRUE(webHostCStringAllowed("192.168.1.2", false, hostname, "192.168.1.2"));
    TEST_ASSERT_TRUE(webHostCStringAllowed("192.168.1.2:8080", false, hostname, "192.168.1.2"));
    TEST_ASSERT_FALSE(webHostCStringAllowed("192.168.1.3", false, hostname, "192.168.1.2"));
    TEST_ASSERT_FALSE(webHostCStringAllowed("chaya2mqtt-a1b2c3", false, nullptr, nullptr));
}

void test_spa_asset_lookup() {
    static const SpaAssetEntry entries[] = {
        {"/", 0u, 10u, "text/html; charset=utf-8", SpaCacheClass::NoCache},
        {"/index.html", 10u, 20u, "text/html; charset=utf-8", SpaCacheClass::NoCache},
        {"/assets/app-abc.js", 30u, 40u, "application/javascript; charset=utf-8", SpaCacheClass::Immutable},
    };
    const size_t count = sizeof(entries) / sizeof(entries[0]);

    TEST_ASSERT_TRUE(spaIsAssetPath("/assets/app-abc.js"));
    TEST_ASSERT_FALSE(spaIsAssetPath("/update"));
    TEST_ASSERT_TRUE(spaIsApiOrEventsPath("/api/bootstrap"));
    TEST_ASSERT_TRUE(spaIsApiOrEventsPath("/events"));
    TEST_ASSERT_FALSE(spaIsApiOrEventsPath("/wifi"));

    TEST_ASSERT_TRUE(spaIsCaptivePortalProbe("/generate_204"));
    TEST_ASSERT_TRUE(spaIsCaptivePortalProbe("/hotspot-detect.html"));
    TEST_ASSERT_TRUE(spaIsCaptivePortalProbe("/ncsi.txt"));
    TEST_ASSERT_TRUE(spaIsCaptivePortalProbe("/wpad.dat"));
    TEST_ASSERT_FALSE(spaIsCaptivePortalProbe("/wifi"));

    TEST_ASSERT_TRUE(spaShouldFallbackToIndex("/update"));
    TEST_ASSERT_TRUE(spaShouldFallbackToIndex("/wifi-testing"));
    TEST_ASSERT_FALSE(spaShouldFallbackToIndex("/api/mqtt"));
    TEST_ASSERT_FALSE(spaShouldFallbackToIndex("/assets/missing.js"));

    const SpaAssetEntry *js = spaFindAsset(entries, count, "/assets/app-abc.js");
    TEST_ASSERT_NOT_NULL(js);
    TEST_ASSERT_EQUAL_UINT32(30u, js->offset);
    TEST_ASSERT_EQUAL_UINT32(40u, js->length);

    const SpaAssetEntry *index = spaFindIndex(entries, count);
    TEST_ASSERT_NOT_NULL(index);
    TEST_ASSERT_EQUAL_STRING("/index.html", index->path);

    TEST_ASSERT_EQUAL_INT(static_cast<int>(SpaCacheClass::Immutable), static_cast<int>(spaCacheClassForPath("/assets/x.css")));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SpaCacheClass::NoCache), static_cast<int>(spaCacheClassForPath("/index.html")));
    TEST_ASSERT_EQUAL_STRING("text/css; charset=utf-8", spaContentTypeForPath("/assets/a.css"));
    TEST_ASSERT_EQUAL_STRING("application/javascript; charset=utf-8", spaContentTypeForPath("/assets/a.js"));
    TEST_ASSERT_FALSE(spaAssetUsesGzip("/index.html"));
    TEST_ASSERT_TRUE(spaAssetUsesGzip("/assets/a.js"));
}

void test_sse_tick_select_bits() {
    bool keepalive = false;
    TEST_ASSERT_EQUAL_UINT32(kSseChaya, sseTickSelectBits(kSseChaya, 1000U, 0U, 8000U, &keepalive));
    TEST_ASSERT_FALSE(keepalive);

    keepalive = true;
    TEST_ASSERT_EQUAL_UINT32(0U, sseTickSelectBits(0U, 1000U, 500U, 8000U, &keepalive));
    TEST_ASSERT_FALSE(keepalive);

    keepalive = false;
    TEST_ASSERT_EQUAL_UINT32(kSseWifi | kSseDevice, sseTickSelectBits(0U, 9000U, 500U, 8000U, &keepalive));
    TEST_ASSERT_TRUE(keepalive);

    keepalive = false;
    TEST_ASSERT_EQUAL_UINT32(kSseWifi | kSseDevice, sseTickSelectBits(0U, 100U, 0U, 8000U, &keepalive));
    TEST_ASSERT_TRUE(keepalive);

    TEST_ASSERT_EQUAL_UINT32(kSseWifi | kSseDevice,
                             sseTickMaskForApMode(kSseChaya | kSseWifi | kSseMqtt | kSseOta | kSseDevice, true));
    TEST_ASSERT_EQUAL_UINT32(kSseChaya | kSseWifi, sseTickMaskForApMode(kSseChaya | kSseWifi, false));
    TEST_ASSERT_TRUE(sseTickForceSnapshot(kSseAll));
    TEST_ASSERT_FALSE(sseTickForceSnapshot(sseTickMaskForApMode(kSseAll, true)));
}

void test_sse_redirty_domains() {
    // RC-WEB-04: failed serialize/send marks these domain bits, which must cover kSseAll.
    TEST_ASSERT_EQUAL_UINT32(kSseAll, kSseChaya | kSseWifi | kSseMqtt | kSseOta | kSseDevice);
    TEST_ASSERT_FALSE(sseEnqueueAccepted(0));
    TEST_ASSERT_TRUE(sseEnqueueAccepted(1));
    TEST_ASSERT_FALSE(sseEnqueueAccepted(2));
}

void test_web_admin_restart_blocked() {
    TEST_ASSERT_FALSE(webAdminRestartBlocked(false, false, false, false, false));
    TEST_ASSERT_TRUE(webAdminRestartBlocked(true, false, false, false, false));
    TEST_ASSERT_TRUE(webAdminRestartBlocked(false, true, false, false, false));
    TEST_ASSERT_TRUE(webAdminRestartBlocked(false, false, true, false, false));
    TEST_ASSERT_TRUE(webAdminRestartBlocked(false, false, false, true, false));
    TEST_ASSERT_TRUE(webAdminRestartBlocked(false, false, false, false, true));
}

void test_web_admin_apply_commit_allowed() {
    TEST_ASSERT_TRUE(webAdminApplyCommitAllowed(false, true));
    TEST_ASSERT_FALSE(webAdminApplyCommitAllowed(true, true));
    TEST_ASSERT_FALSE(webAdminApplyCommitAllowed(false, false));
    TEST_ASSERT_FALSE(webAdminApplyCommitAllowed(true, false));
    TEST_ASSERT_FALSE(webAdminApplyCommitAllowed(false, true, true));
}

void test_admin_apply_blocked_by_ota() {
    TEST_ASSERT_FALSE(adminApplyBlockedByOta(false));
    TEST_ASSERT_TRUE(adminApplyBlockedByOta(true));
    TEST_ASSERT_TRUE(webAdminDeferredApplyAllowed(false, false));
    TEST_ASSERT_FALSE(webAdminDeferredApplyAllowed(true, false));
    TEST_ASSERT_FALSE(webAdminDeferredApplyAllowed(false, true));
    TEST_ASSERT_FALSE(webAdminDeferredApplyAllowed(true, true));
    TEST_ASSERT_FALSE(webAdminDeferredApplyAllowed(false, false, true));
}

void test_web_admin_ota_start_blocked_by_apply() {
    TEST_ASSERT_FALSE(webAdminOtaStartBlocked(false, false, false, false));
    TEST_ASSERT_TRUE(webAdminOtaStartBlocked(true, false, false, false));
    TEST_ASSERT_TRUE(webAdminOtaStartBlocked(false, true, false, false));
    TEST_ASSERT_TRUE(webAdminOtaStartBlocked(false, false, true, false));
    TEST_ASSERT_TRUE(webAdminOtaStartBlocked(false, false, false, true));
}

void test_factory_reset_http_blocked() {
    TEST_ASSERT_FALSE(factoryResetHttpBlocked(false, false, false, false, false));
    TEST_ASSERT_TRUE(factoryResetHttpBlocked(true, false, false, false, false));
    TEST_ASSERT_TRUE(factoryResetHttpBlocked(false, true, false, false, false));
    TEST_ASSERT_TRUE(factoryResetHttpBlocked(false, false, true, false, false));
    TEST_ASSERT_TRUE(factoryResetHttpBlocked(false, false, false, true, false));
    TEST_ASSERT_TRUE(factoryResetHttpBlocked(false, false, false, false, true));
}

void test_soft_off_allowed() {
    TEST_ASSERT_TRUE(softOffAllowed(false, false, false));
    TEST_ASSERT_FALSE(softOffAllowed(true, false, false));
    TEST_ASSERT_FALSE(softOffAllowed(false, true, false));
    TEST_ASSERT_FALSE(softOffAllowed(false, false, true));
    TEST_ASSERT_FALSE(softOffAllowed(false, false, false, true, false, false));
    TEST_ASSERT_FALSE(softOffAllowed(false, false, false, false, true, false));
    TEST_ASSERT_FALSE(softOffAllowed(false, false, false, false, false, true));
}

void test_web_admin_mqtt_apply_unqueued() {
    TEST_ASSERT_FALSE(webAdminMqttApplyUnqueuedPure(0U, 0U));
    TEST_ASSERT_FALSE(webAdminMqttApplyUnqueuedPure(1U, 1U));
    TEST_ASSERT_TRUE(webAdminMqttApplyUnqueuedPure(2U, 1U));
    TEST_ASSERT_FALSE(webAdminMqttApplyUnqueuedPure(1U, 2U));
}

void test_sse_mqtt_page_conn() {
    TEST_ASSERT_FALSE(mqttPageConn(false, false));
    TEST_ASSERT_FALSE(mqttPageConn(false, true));
    TEST_ASSERT_FALSE(mqttPageConn(true, false));
    TEST_ASSERT_TRUE(mqttPageConn(true, true));
}

void test_wifi_sta_net_resolve_uses_last_good_on_failure() {
    WifiStaNetCache cache{};
    WifiStaNetFields live{};
    live.connected = true;
    std::strncpy(live.ssid, "Cafe", sizeof(live.ssid) - 1U);
    std::strncpy(live.ip, "192.168.1.10", sizeof(live.ip) - 1U);
    std::strncpy(live.gateway, "192.168.1.1", sizeof(live.gateway) - 1U);
    live.rssi = -50;

    TEST_ASSERT_EQUAL_INT(static_cast<int>(WifiStaNetResolve::Fresh),
                          static_cast<int>(wifiStaNetResolveSnapshot(true, &cache, &live)));
    TEST_ASSERT_TRUE(cache.have);
    TEST_ASSERT_TRUE(cache.fields.connected);

    live = {};
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WifiStaNetResolve::Cached),
                          static_cast<int>(wifiStaNetResolveSnapshot(false, &cache, &live)));
    TEST_ASSERT_TRUE(live.connected);
    TEST_ASSERT_EQUAL_STRING("Cafe", live.ssid);
    TEST_ASSERT_EQUAL_STRING("192.168.1.10", live.ip);
    TEST_ASSERT_EQUAL_STRING("192.168.1.1", live.gateway);
    TEST_ASSERT_EQUAL_INT(-50, live.rssi);
}

void test_wifi_sta_net_resolve_none_without_cache() {
    WifiStaNetCache cache{};
    WifiStaNetFields live{};
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WifiStaNetResolve::None),
                          static_cast<int>(wifiStaNetResolveSnapshot(false, &cache, &live)));
    TEST_ASSERT_FALSE(live.connected);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WifiStaNetResolve::None),
                          static_cast<int>(wifiStaNetResolveSnapshot(true, nullptr, &live)));
}

void test_wifi_sta_net_resolve_stores_disconnected() {
    WifiStaNetCache cache{};
    WifiStaNetFields live{};
    live.connected = true;
    std::strncpy(live.ssid, "Cafe", sizeof(live.ssid) - 1U);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WifiStaNetResolve::Fresh),
                          static_cast<int>(wifiStaNetResolveSnapshot(true, &cache, &live)));

    live = {};
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WifiStaNetResolve::Fresh),
                          static_cast<int>(wifiStaNetResolveSnapshot(true, &cache, &live)));
    TEST_ASSERT_TRUE(cache.have);
    TEST_ASSERT_FALSE(cache.fields.connected);
    TEST_ASSERT_EQUAL_STRING("", cache.fields.ssid);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_ui_pref_syntax);
    RUN_TEST(test_settings_range_helpers);
    RUN_TEST(test_host_validate);
    RUN_TEST(test_spa_asset_lookup);
    RUN_TEST(test_sse_tick_select_bits);
    RUN_TEST(test_sse_redirty_domains);
    RUN_TEST(test_web_admin_restart_blocked);
    RUN_TEST(test_web_admin_apply_commit_allowed);
    RUN_TEST(test_admin_apply_blocked_by_ota);
    RUN_TEST(test_web_admin_ota_start_blocked_by_apply);
    RUN_TEST(test_factory_reset_http_blocked);
    RUN_TEST(test_soft_off_allowed);
    RUN_TEST(test_web_admin_mqtt_apply_unqueued);
    RUN_TEST(test_sse_mqtt_page_conn);
    RUN_TEST(test_wifi_sta_net_resolve_uses_last_good_on_failure);
    RUN_TEST(test_wifi_sta_net_resolve_none_without_cache);
    RUN_TEST(test_wifi_sta_net_resolve_stores_disconnected);
    return UNITY_END();
}
