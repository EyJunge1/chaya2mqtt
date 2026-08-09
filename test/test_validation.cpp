#include <unity.h>

#include "constants.h"
#include "util/time_helpers.h"
#include "heart/counter.h"
#include "ota/version_cmp.h"
#include "ota/github_parse.h"

void test_calendar_day_since_epoch() {
    TEST_ASSERT_EQUAL_UINT32(0U, calendarDaySinceEpochUtc(0));
    TEST_ASSERT_EQUAL_UINT32(1U, calendarDaySinceEpochUtc(86400));
}

void test_mqtt_topic_syntax() {
    TEST_ASSERT_TRUE(mqttTopicSyntaxOk("chaya/to_a", 128));
    TEST_ASSERT_FALSE(mqttTopicSyntaxOk("", 128));
    TEST_ASSERT_FALSE(mqttTopicSyntaxOk("bad topic", 128));
    TEST_ASSERT_FALSE(mqttTopicSyntaxOk("bad#topic", 128));
}

void test_mqtt_server_syntax() {
    TEST_ASSERT_TRUE(mqttServerSyntaxOk("broker.example.com", 128));
    TEST_ASSERT_FALSE(mqttServerSyntaxOk("bad\x7Fhost", 128));
    TEST_ASSERT_FALSE(mqttServerSyntaxOk("bad host", 128));
}

void test_device_id_syntax() {
    TEST_ASSERT_TRUE(deviceIdSyntaxOk("a1b2c3"));
    TEST_ASSERT_FALSE(deviceIdSyntaxOk("A1B2C3"));
    TEST_ASSERT_FALSE(deviceIdSyntaxOk("abc"));
}

void test_wifi_ssid_syntax() {
    TEST_ASSERT_TRUE(wifiSsidSyntaxOk("MyNetwork", 33));
    TEST_ASSERT_FALSE(wifiSsidSyntaxOk("bad\x01", 33));
}

void test_ui_pref_syntax() {
    TEST_ASSERT_TRUE(uiLangSyntaxOk("de"));
    TEST_ASSERT_TRUE(uiLangSyntaxOk("en"));
    TEST_ASSERT_FALSE(uiLangSyntaxOk("fr"));
    TEST_ASSERT_FALSE(uiLangSyntaxOk(nullptr));
    TEST_ASSERT_TRUE(uiThemeSyntaxOk("dark"));
    TEST_ASSERT_TRUE(uiThemeSyntaxOk("light"));
    TEST_ASSERT_FALSE(uiThemeSyntaxOk("system"));
    TEST_ASSERT_FALSE(uiThemeSyntaxOk(nullptr));
}

void test_deadline_helpers() {
    TEST_ASSERT_FALSE(deadlineReached(1000U, 500U, 1200U));
    TEST_ASSERT_TRUE(deadlineReached(1000U, 500U, 1500U));
    TEST_ASSERT_EQUAL_UINT32(300U, remainingMs(1000U, 500U, 1200U));
    TEST_ASSERT_EQUAL_UINT32(0U, remainingMs(1000U, 500U, 2000U));
}

void test_ota_version_compare() {
    TEST_ASSERT_TRUE(otaVersionIsNewer("v2026.8.2", "2026.8.1"));
    TEST_ASSERT_FALSE(otaVersionIsNewer("v2026.8.1", "2026.8.1"));
    TEST_ASSERT_FALSE(otaVersionIsNewer("v2026.8.0", "2026.8.1"));
    TEST_ASSERT_TRUE(otaVersionIsNewer("v2026.8.1", "2026.8.1-rc.1"));
    TEST_ASSERT_FALSE(otaVersionIsNewer("v2026.8.1-rc.2", "2026.8.1"));
    TEST_ASSERT_TRUE(otaVersionIsNewer("v2026.8.1-rc.2", "2026.8.1-rc.1"));
    TEST_ASSERT_TRUE(otaVersionIsRc("2026.8.1-rc.1"));
    TEST_ASSERT_FALSE(otaVersionIsRc("2026.8.1"));
}

void test_ota_github_release_select() {
    const char* json =
        "["
        "{\"tag_name\":\"v2026.8.2-rc.1\",\"draft\":false,\"prerelease\":true,"
        "\"assets\":[{\"name\":\"firmware.bin\"},{\"name\":\"firmware.md5\"}]},"
        "{\"tag_name\":\"v2026.8.1\",\"draft\":false,\"prerelease\":false,"
        "\"assets\":[{\"name\":\"firmware.bin\"},{\"name\":\"firmware.md5\"}]}"
        "]";

    char tag[64]{};
    bool isPre = false;
    TEST_ASSERT_TRUE(otaSelectReleaseFromListJson(json, true, tag, sizeof(tag), &isPre));
    TEST_ASSERT_EQUAL_STRING("v2026.8.2-rc.1", tag);
    TEST_ASSERT_TRUE(isPre);

    TEST_ASSERT_TRUE(otaSelectReleaseFromListJson(json, false, tag, sizeof(tag), &isPre));
    TEST_ASSERT_EQUAL_STRING("v2026.8.1", tag);
    TEST_ASSERT_FALSE(isPre);

    const char* onlyStable =
        "[{\"tag_name\":\"v2026.8.1\",\"draft\":false,\"prerelease\":false}]";
    TEST_ASSERT_TRUE(otaSelectReleaseFromListJson(onlyStable, true, tag, sizeof(tag), &isPre));
    TEST_ASSERT_EQUAL_STRING("v2026.8.1", tag);
    TEST_ASSERT_FALSE(isPre);

    TEST_ASSERT_TRUE(otaJsonHasAssetName(json, "firmware.bin"));
    TEST_ASSERT_TRUE(otaJsonHasAssetName(json, "firmware.md5"));
    TEST_ASSERT_FALSE(otaJsonHasAssetName(json, "missing.bin"));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_calendar_day_since_epoch);
    RUN_TEST(test_mqtt_topic_syntax);
    RUN_TEST(test_mqtt_server_syntax);
    RUN_TEST(test_device_id_syntax);
    RUN_TEST(test_wifi_ssid_syntax);
    RUN_TEST(test_ui_pref_syntax);
    RUN_TEST(test_deadline_helpers);
    RUN_TEST(test_ota_version_compare);
    RUN_TEST(test_ota_github_release_select);
    return UNITY_END();
}
