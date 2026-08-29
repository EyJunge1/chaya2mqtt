#include <cstring>
#include <unity.h>

#include "constants.h"
#include "identity/device_identity.h"
#include "identity/device_identity_pure.h"
#include "mqtt/backoff.h"
#include "mqtt/counter_payload.h"
#include "mqtt/mqtt_config.h"
#include "mqtt/mqtt_publish_ack.h"
#include "mqtt/pairing.h"

void test_mqtt_topic_syntax() {
    TEST_ASSERT_TRUE(mqttTopicSyntaxOk("chaya2mqtt/a1b2c3", 128));
    TEST_ASSERT_FALSE(mqttTopicSyntaxOk("", 128));
    TEST_ASSERT_FALSE(mqttTopicSyntaxOk("bad topic", 128));
    TEST_ASSERT_FALSE(mqttTopicSyntaxOk("bad#topic", 128));
    TEST_ASSERT_FALSE(mqttTopicSyntaxOk("bad+topic", 128));
    TEST_ASSERT_FALSE(mqttTopicSyntaxOk(nullptr, 128));
}

void test_mqtt_server_syntax() {
    TEST_ASSERT_TRUE(mqttServerSyntaxOk("broker.example.com", 128));
    TEST_ASSERT_FALSE(mqttServerSyntaxOk("bad\x7Fhost", 128));
    TEST_ASSERT_FALSE(mqttServerSyntaxOk("bad host", 128));
    TEST_ASSERT_FALSE(mqttServerSyntaxOk("", 128));
}

void test_device_id_syntax() {
    TEST_ASSERT_TRUE(deviceIdSyntaxOk("a1b2c3"));
    TEST_ASSERT_FALSE(deviceIdSyntaxOk("A1B2C3"));
    TEST_ASSERT_FALSE(deviceIdSyntaxOk("abc"));
    TEST_ASSERT_FALSE(deviceIdSyntaxOk("a1b2c3d"));
    TEST_ASSERT_FALSE(deviceIdSyntaxOk(nullptr));
}

void test_device_id_create_mode() {
    TEST_ASSERT_EQUAL(static_cast<int>(DeviceIdCreateMode::FromMacMigrate),
                      static_cast<int>(deviceIdCreateMode(true)));
    TEST_ASSERT_EQUAL(static_cast<int>(DeviceIdCreateMode::FromRandom),
                      static_cast<int>(deviceIdCreateMode(false)));
}

void test_device_id_format_from_bytes() {
    const uint8_t bytes[3] = {0xa1, 0xb2, 0xc3};
    char id[kDeviceIdBufLen]{};
    TEST_ASSERT_TRUE(deviceIdFormatFromBytes(bytes, id, sizeof(id)));
    TEST_ASSERT_EQUAL_STRING("a1b2c3", id);

    char tooSmall[kDeviceIdHexLen]{};
    TEST_ASSERT_FALSE(deviceIdFormatFromBytes(bytes, tooSmall, sizeof(tooSmall)));
    TEST_ASSERT_EQUAL_STRING("", tooSmall);

    TEST_ASSERT_FALSE(deviceIdFormatFromBytes(nullptr, id, sizeof(id)));
    TEST_ASSERT_FALSE(deviceIdFormatFromBytes(bytes, nullptr, sizeof(id)));
}

void test_device_sta_hostname_format() {
    char hostname[kDeviceStaHostnameBufLen]{};
    TEST_ASSERT_TRUE(formatDeviceStaHostname("a1b2c3", hostname, sizeof(hostname)));
    TEST_ASSERT_EQUAL_STRING("chaya2mqtt-a1b2c3", hostname);

    hostname[0] = 'x';
    TEST_ASSERT_FALSE(formatDeviceStaHostname("A1B2C3", hostname, sizeof(hostname)));
    TEST_ASSERT_EQUAL_STRING("", hostname);

    char tooSmall[kDeviceStaHostnameBufLen - 1U]{};
    TEST_ASSERT_FALSE(formatDeviceStaHostname("a1b2c3", tooSmall, sizeof(tooSmall)));
    TEST_ASSERT_EQUAL_STRING("", tooSmall);
}

void test_normalize_mqtt_port() {
    TEST_ASSERT_EQUAL_UINT16(8883, normalizeMqttPort(0));
    TEST_ASSERT_EQUAL_UINT16(8883, normalizeMqttPort(-1));
    TEST_ASSERT_EQUAL_UINT16(8883, normalizeMqttPort(70000));
    TEST_ASSERT_EQUAL_UINT16(1883, normalizeMqttPort(1883));
    TEST_ASSERT_EQUAL_UINT16(65535, normalizeMqttPort(65535));
}

void test_pairing_topics() {
    MqttConfig cfg{};
    std::strncpy(cfg.partnerDeviceId, "f5e6d7", sizeof(cfg.partnerDeviceId));
    mqttApplyPairingTopicsWithIds(&cfg, "a1b2c3");
    TEST_ASSERT_EQUAL_STRING("chaya2mqtt/a1b2c3", cfg.topicPub);
    TEST_ASSERT_EQUAL_STRING("chaya2mqtt/f5e6d7", cfg.topicSub);

    cfg.partnerDeviceId[0] = '\0';
    mqttApplyPairingTopicsWithIds(&cfg, "a1b2c3");
    TEST_ASSERT_EQUAL_STRING("chaya2mqtt/a1b2c3", cfg.topicPub);
    TEST_ASSERT_EQUAL_STRING("", cfg.topicSub);
}

void test_sanitize_partner_and_server() {
    MqttConfig cfg{};
    std::strncpy(cfg.server, "bad host", sizeof(cfg.server));
    std::strncpy(cfg.partnerDeviceId, "A1B2C3", sizeof(cfg.partnerDeviceId));
    cfg.port = 0;
    mqttSanitizeConfigAfterLoad(cfg, "f5e6d7");
    TEST_ASSERT_EQUAL_STRING("", cfg.server);
    TEST_ASSERT_EQUAL_STRING("a1b2c3", cfg.partnerDeviceId);
    TEST_ASSERT_EQUAL_UINT16(8883, cfg.port);
    TEST_ASSERT_EQUAL_STRING("chaya2mqtt/f5e6d7", cfg.topicPub);
    TEST_ASSERT_EQUAL_STRING("chaya2mqtt/a1b2c3", cfg.topicSub);

    std::strncpy(cfg.partnerDeviceId, "f5e6d7", sizeof(cfg.partnerDeviceId));
    mqttSanitizePartnerId(cfg, "f5e6d7");
    TEST_ASSERT_EQUAL_STRING("", cfg.partnerDeviceId);
}

void test_counter_payload_parse() {
    long v = -1;
    TEST_ASSERT_TRUE(mqttParseCounterPayload("42", 2, &v));
    TEST_ASSERT_EQUAL_INT(42, static_cast<int>(v));
    TEST_ASSERT_FALSE(mqttParseCounterPayload("", 0, &v));
    TEST_ASSERT_FALSE(mqttParseCounterPayload("12a", 3, &v));
    TEST_ASSERT_FALSE(mqttParseCounterPayload("12345678901", 11, &v));
    TEST_ASSERT_TRUE(mqttParseCounterPayload("0", 1, &v));
    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(v));
}

void test_backoff_helpers() {
    MqttBackoffState st{};
    TEST_ASSERT_TRUE(mqttBackoffElapsed(st, 0));
    const unsigned long wait = mqttNextFailureBackoffMs(st, false);
    TEST_ASSERT_EQUAL_UINT32(kMqttBackoffInitialMs, wait);
    TEST_ASSERT_TRUE(st.currentBackoffMs > kMqttBackoffInitialMs
                     || st.currentBackoffMs == kMqttBackoffMaxMs);

    mqttBackoffResetOnConnect(st);
    TEST_ASSERT_EQUAL_UINT32(kMqttBackoffInitialMs, st.currentBackoffMs);

    MqttBackoffState wifiSt{};
    const unsigned long wifiWait = mqttNextFailureBackoffMs(wifiSt, true);
    TEST_ASSERT_TRUE(wifiWait >= kMqttWifiLostDuringTlsBackoffMs);

    TEST_ASSERT_EQUAL_UINT32(kMqttBrokerMissingBackoffMs,
                             mqttConnectPrecheckDeferMsPure(false, true, true, true));
    TEST_ASSERT_EQUAL_UINT32(kMqttWifiDownBackoffMs,
                             mqttConnectPrecheckDeferMsPure(true, false, false, false));
    TEST_ASSERT_EQUAL_UINT32(kMqttNtpRetryMs,
                             mqttConnectPrecheckDeferMsPure(true, true, false, true));
    TEST_ASSERT_EQUAL_UINT32(0U, mqttConnectPrecheckDeferMsPure(true, true, true, true));
}

void test_publish_ack_state() {
    MqttPublishAckState state{};
    TEST_ASSERT_TRUE(mqttPublishAckBegin(&state, 7, 3U, 42));
    TEST_ASSERT_TRUE(mqttPublishAckIsPending(state));
    TEST_ASSERT_FALSE(mqttPublishAckBegin(&state, 8, 3U, 42));
    TEST_ASSERT_FALSE(mqttPublishAckConfirm(&state, 8, 3U));
    TEST_ASSERT_FALSE(mqttPublishAckConfirm(&state, 7, 4U));
    TEST_ASSERT_TRUE(mqttPublishAckConfirm(&state, 7, 3U));
    TEST_ASSERT_TRUE(mqttPublishAckWasConfirmed(state, 7, 3U));
    TEST_ASSERT_FALSE(mqttPublishAckConfirm(&state, 7, 3U));

    TEST_ASSERT_TRUE(mqttPublishAckBegin(&state, 9, 4U, 43));
    TEST_ASSERT_FALSE(mqttPublishAckFail(&state, 3U));
    TEST_ASSERT_TRUE(mqttPublishAckFail(&state, 4U));
    TEST_ASSERT_FALSE(mqttPublishAckIsPending(state));
    TEST_ASSERT_FALSE(mqttPublishAckWasConfirmed(state, 9, 4U));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_mqtt_topic_syntax);
    RUN_TEST(test_mqtt_server_syntax);
    RUN_TEST(test_device_id_syntax);
    RUN_TEST(test_device_id_create_mode);
    RUN_TEST(test_device_id_format_from_bytes);
    RUN_TEST(test_device_sta_hostname_format);
    RUN_TEST(test_normalize_mqtt_port);
    RUN_TEST(test_pairing_topics);
    RUN_TEST(test_sanitize_partner_and_server);
    RUN_TEST(test_counter_payload_parse);
    RUN_TEST(test_backoff_helpers);
    RUN_TEST(test_publish_ack_state);
    return UNITY_END();
}
