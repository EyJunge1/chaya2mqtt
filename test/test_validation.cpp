#include <unity.h>

#include "constants.h"
#include "util/time_helpers.h"
#include "heart/counter.h"

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

void test_deadline_helpers() {
    TEST_ASSERT_FALSE(deadlineReached(1000U, 500U, 1200U));
    TEST_ASSERT_TRUE(deadlineReached(1000U, 500U, 1500U));
    TEST_ASSERT_EQUAL_UINT32(300U, remainingMs(1000U, 500U, 1200U));
    TEST_ASSERT_EQUAL_UINT32(0U, remainingMs(1000U, 500U, 2000U));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_calendar_day_since_epoch);
    RUN_TEST(test_mqtt_topic_syntax);
    RUN_TEST(test_mqtt_server_syntax);
    RUN_TEST(test_device_id_syntax);
    RUN_TEST(test_wifi_ssid_syntax);
    RUN_TEST(test_deadline_helpers);
    return UNITY_END();
}
