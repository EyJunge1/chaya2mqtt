#include <cstring>
#include <unity.h>

#include "constants.h"
#include "identity/device_identity.h"
#include "identity/device_identity_pure.h"
#include "mqtt/mqtt.h"
#include "mqtt/backoff.h"
#include "mqtt/mqtt_event_live_pure.h"
#include "mqtt/counter_payload.h"
#include "mqtt/mqtt_apply_pure.h"
#include "mqtt/mqtt_config.h"
#include "mqtt/mqtt_pack.h"
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

void test_mqtt_username_syntax() {
    TEST_ASSERT_TRUE(mqttUsernameSyntaxOk("", 64));
    TEST_ASSERT_TRUE(mqttUsernameSyntaxOk("chaya", 64));
    TEST_ASSERT_FALSE(mqttUsernameSyntaxOk("bad\x01user", 64));
    TEST_ASSERT_TRUE(mqttUsernameSyntaxOk("\xc3\xbcser", 64)); // UTF-8 broker user
    TEST_ASSERT_FALSE(mqttUsernameSyntaxOk(nullptr, 64));
    char tooLong[65];
    memset(tooLong, 'a', 64);
    tooLong[64] = '\0';
    TEST_ASSERT_FALSE(mqttUsernameSyntaxOk(tooLong, 64));
}

void test_mqtt_password_syntax() {
    TEST_ASSERT_TRUE(mqttPasswordSyntaxOk("", 64));
    TEST_ASSERT_TRUE(mqttPasswordSyntaxOk("s3cret!@#", 64));
    TEST_ASSERT_FALSE(mqttPasswordSyntaxOk("bad\npass", 64));
    TEST_ASSERT_FALSE(mqttPasswordSyntaxOk("bad\x01pass", 64));
    TEST_ASSERT_FALSE(mqttPasswordSyntaxOk(nullptr, 64));
}

void test_device_id_syntax() {
    TEST_ASSERT_TRUE(deviceIdSyntaxOk("a1b2c3"));
    TEST_ASSERT_FALSE(deviceIdSyntaxOk("A1B2C3"));
    TEST_ASSERT_FALSE(deviceIdSyntaxOk("abc"));
    TEST_ASSERT_FALSE(deviceIdSyntaxOk("a1b2c3d"));
    TEST_ASSERT_FALSE(deviceIdSyntaxOk(nullptr));
}

void test_device_id_create_mode() {
    TEST_ASSERT_EQUAL(static_cast<int>(DeviceIdCreateMode::FromMacMigrate), static_cast<int>(deviceIdCreateMode(true)));
    TEST_ASSERT_EQUAL(static_cast<int>(DeviceIdCreateMode::FromRandom), static_cast<int>(deviceIdCreateMode(false)));
}

void test_had_prior_mqtt_setup_keys() {
    TEST_ASSERT_FALSE(hadPriorMqttSetupKeys(false, false));
    TEST_ASSERT_TRUE(hadPriorMqttSetupKeys(true, false));
    TEST_ASSERT_TRUE(hadPriorMqttSetupKeys(false, true));
    TEST_ASSERT_TRUE(hadPriorMqttSetupKeys(true, true));
}

void test_mqtt_chaya_publish_try_is_fail() {
    TEST_ASSERT_FALSE(mqttChayaPublishTryIsFail(MqttChayaPublishTry::Ok));
    TEST_ASSERT_FALSE(mqttChayaPublishTryIsFail(MqttChayaPublishTry::Retry));
    TEST_ASSERT_TRUE(mqttChayaPublishTryIsFail(MqttChayaPublishTry::Fail));
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
    TEST_ASSERT_EQUAL_UINT16(8883, mqttDefaultPortForTls(true));
    TEST_ASSERT_EQUAL_UINT16(1883, mqttDefaultPortForTls(false));
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
    TEST_ASSERT_TRUE(cfg.tls);
    std::strncpy(cfg.server, "bad host", sizeof(cfg.server));
    std::strncpy(cfg.partnerDeviceId, "A1B2C3", sizeof(cfg.partnerDeviceId));
    cfg.port = 0;
    mqttSanitizeConfigAfterLoad(cfg, "f5e6d7");
    TEST_ASSERT_EQUAL_STRING("", cfg.server);
    TEST_ASSERT_EQUAL_STRING("a1b2c3", cfg.partnerDeviceId);
    TEST_ASSERT_EQUAL_UINT16(8883, cfg.port);
    TEST_ASSERT_TRUE(cfg.tls);
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
    TEST_ASSERT_TRUE(st.currentBackoffMs > kMqttBackoffInitialMs || st.currentBackoffMs == kMqttBackoffMaxMs);

    mqttBackoffResetOnConnect(st);
    TEST_ASSERT_EQUAL_UINT32(kMqttBackoffInitialMs, st.currentBackoffMs);

    MqttBackoffState wifiSt{};
    const unsigned long wifiWait = mqttNextFailureBackoffMs(wifiSt, true);
    TEST_ASSERT_TRUE(wifiWait >= kMqttWifiLostDuringTlsBackoffMs);

    TEST_ASSERT_EQUAL_UINT32(kMqttBrokerMissingBackoffMs, mqttConnectPrecheckDeferMsPure(false, true, true, true));
    TEST_ASSERT_EQUAL_UINT32(kMqttWifiDownBackoffMs, mqttConnectPrecheckDeferMsPure(true, false, false, false));
    TEST_ASSERT_EQUAL_UINT32(kMqttNtpRetryMs, mqttConnectPrecheckDeferMsPure(true, true, false, true));
    TEST_ASSERT_EQUAL_UINT32(0U, mqttConnectPrecheckDeferMsPure(true, true, true, true));
}

void test_publish_ack_state() {
    MqttPublishAckState state{};
    TEST_ASSERT_TRUE(mqttPublishAckCanBegin(state));
    TEST_ASSERT_FALSE(mqttPublishAckBlocksNewPublish(state));
    TEST_ASSERT_TRUE(mqttPublishAckBegin(&state, 7, 3U, 42));
    TEST_ASSERT_TRUE(mqttPublishAckIsPending(state));
    TEST_ASSERT_TRUE(mqttPublishAckBlocksNewPublish(state));
    TEST_ASSERT_FALSE(mqttPublishAckCanBegin(state));
    TEST_ASSERT_FALSE(mqttPublishAckBegin(&state, 8, 3U, 42));
    TEST_ASSERT_FALSE(mqttPublishAckConfirm(&state, 8, 3U));
    TEST_ASSERT_FALSE(mqttPublishAckConfirm(&state, 7, 4U));
    TEST_ASSERT_TRUE(mqttPublishAckConfirm(&state, 7, 3U));
    TEST_ASSERT_TRUE(mqttPublishAckWasConfirmed(state, 7, 3U));
    TEST_ASSERT_EQUAL_INT(42, state.expectedCounter);
    TEST_ASSERT_FALSE(mqttPublishAckConfirm(&state, 7, 3U));
    TEST_ASSERT_FALSE(mqttPublishAckBegin(&state, 9, 4U, 43));
    TEST_ASSERT_TRUE(mqttPublishAckBlocksNewPublish(state));
    TEST_ASSERT_FALSE(mqttPublishAckCanBegin(state));

    MqttPublishAckState failed{};
    TEST_ASSERT_TRUE(mqttPublishAckBegin(&failed, 9, 4U, 43));
    TEST_ASSERT_FALSE(mqttPublishAckFail(&failed, 3U));
    TEST_ASSERT_TRUE(mqttPublishAckFail(&failed, 4U));
    TEST_ASSERT_FALSE(mqttPublishAckIsPending(failed));
    TEST_ASSERT_FALSE(mqttPublishAckWasConfirmed(failed, 9, 4U));
    TEST_ASSERT_TRUE(mqttPublishAckCanBegin(failed));
    TEST_ASSERT_FALSE(mqttPublishAckBlocksNewPublish(failed));

    TEST_ASSERT_TRUE(mqttPublishAckBegin(&failed, 1, 0U, 10));
    TEST_ASSERT_TRUE(mqttPublishAckIsPending(failed));
    TEST_ASSERT_EQUAL_UINT32(0U, failed.clientGeneration);
    TEST_ASSERT_TRUE(mqttPublishAckFail(&failed, 0U));
    TEST_ASSERT_FALSE(mqttPublishAckIsPending(failed));
}

void test_publish_ack_begin_blocked_after_confirm() {
    MqttPublishAckState state{};
    TEST_ASSERT_TRUE(mqttPublishAckBegin(&state, 5, 1U, 1));
    TEST_ASSERT_TRUE(mqttPublishAckConfirm(&state, 5, 1U));
    TEST_ASSERT_TRUE(mqttPublishAckWasConfirmed(state, 5, 1U));
    TEST_ASSERT_FALSE(mqttPublishAckBegin(&state, 6, 1U, 2));
    TEST_ASSERT_TRUE(mqttPublishAckWasConfirmed(state, 5, 1U));
    TEST_ASSERT_FALSE(mqttPublishAckConfirm(&state, 5, 1U));
    TEST_ASSERT_EQUAL(static_cast<int>(MqttPublishAckStatus::Acked), static_cast<int>(state.status));
    TEST_ASSERT_TRUE(mqttPublishAckBlocksNewPublish(state));
    TEST_ASSERT_FALSE(mqttPublishAckCanBegin(state));
}

void test_mqtt_pack_roundtrip() {
    MqttConfig cfg{};
    std::strncpy(cfg.server, "broker.example.com", sizeof(cfg.server));
    cfg.port = 1883;
    cfg.tls = false;
    std::strncpy(cfg.username, "user", sizeof(cfg.username));
    std::strncpy(cfg.password, "roundtrip", sizeof(cfg.password));
    std::strncpy(cfg.partnerDeviceId, "f5e6d7", sizeof(cfg.partnerDeviceId));
    std::strncpy(cfg.topicPub, "chaya2mqtt/a1b2c3", sizeof(cfg.topicPub));
    std::strncpy(cfg.topicSub, "chaya2mqtt/f5e6d7", sizeof(cfg.topicSub));

    PackedMqttConfigV1 pk{};
    mqttPackConfigV1(cfg, &pk);
    TEST_ASSERT_EQUAL_UINT32(kMqttCfgPackedMagic, pk.magic);

    MqttConfig out{};
    TEST_ASSERT_TRUE(mqttUnpackConfigV1(pk, &out));
    TEST_ASSERT_EQUAL_STRING("broker.example.com", out.server);
    TEST_ASSERT_EQUAL_UINT16(1883, out.port);
    TEST_ASSERT_FALSE(out.tls);
    TEST_ASSERT_EQUAL_STRING("user", out.username);
    TEST_ASSERT_EQUAL_STRING("roundtrip", out.password);
    TEST_ASSERT_EQUAL_STRING("f5e6d7", out.partnerDeviceId);
    TEST_ASSERT_EQUAL_STRING("", out.topicPub);
    TEST_ASSERT_EQUAL_STRING("", out.topicSub);
}

void test_mqtt_pack_reject_bad_magic() {
    MqttConfig cfg{};
    std::strncpy(cfg.server, "broker.example.com", sizeof(cfg.server));
    PackedMqttConfigV1 pk{};
    mqttPackConfigV1(cfg, &pk);
    pk.magic = 0;
    MqttConfig out{};
    TEST_ASSERT_FALSE(mqttUnpackConfigV1(pk, &out));

    PackedMqttConfigV1 unterminated{};
    unterminated.magic = kMqttCfgPackedMagic;
    std::memset(unterminated.server, 'a', sizeof(unterminated.server));
    TEST_ASSERT_FALSE(mqttUnpackConfigV1(unterminated, &out));
}

void test_mqtt_settings_apply_clear_pending() {
    TEST_ASSERT_TRUE(mqttSettingsApplyShouldClearPending(false));
    TEST_ASSERT_FALSE(mqttSettingsApplyShouldClearPending(true));
    TEST_ASSERT_TRUE(mqttSettingsApplyShouldFinish(true));
    TEST_ASSERT_FALSE(mqttSettingsApplyShouldFinish(false));
    TEST_ASSERT_FALSE(mqttSettingsApplyShouldDefer(false, false, false));
    TEST_ASSERT_TRUE(mqttSettingsApplyShouldDefer(true, false, false));
    TEST_ASSERT_TRUE(mqttSettingsApplyShouldDefer(false, true, false));
    TEST_ASSERT_TRUE(mqttSettingsApplyShouldDefer(false, false, true));
    TEST_ASSERT_FALSE(mqttSettingsApplyNothingPendingNeedsRetry(false));
    TEST_ASSERT_TRUE(mqttSettingsApplyNothingPendingNeedsRetry(true));
}

void test_publish_ack_begin_blocked_when_async_not_pending() {
    MqttPublishAckState state{};
    TEST_ASSERT_TRUE(mqttPublishAckCanBegin(state));
    TEST_ASSERT_TRUE(mqttPublishAckBeginAllowed(true, true));
    TEST_ASSERT_FALSE(mqttPublishAckBeginAllowed(true, false));
    TEST_ASSERT_FALSE(mqttPublishAckBeginAllowed(false, true));
    // Abort without a pending ACK leaves Idle/Failed (CanBegin true) but async is no longer Pending.
    TEST_ASSERT_FALSE(mqttPublishAckBeginAllowed(mqttPublishAckCanBegin(state), false));
    TEST_ASSERT_FALSE(mqttPublishAckConfirm(&state, 4, 1U));
}

void test_abort_may_fail_async() {
    TEST_ASSERT_TRUE(mqttAbortMayFailAsync(false, false));
    TEST_ASSERT_TRUE(mqttAbortMayFailAsync(false, true));
    TEST_ASSERT_TRUE(mqttAbortMayFailAsync(true, true));
    TEST_ASSERT_FALSE(mqttAbortMayFailAsync(true, false));

    MqttPublishAckState state{};
    TEST_ASSERT_TRUE(mqttPublishAckBegin(&state, 3, 1U, 9));
    TEST_ASSERT_TRUE(mqttPublishAckConfirm(&state, 3, 1U));
    TEST_ASSERT_FALSE(mqttPublishAckFail(&state, 1U));
    TEST_ASSERT_TRUE(mqttPublishAckWasConfirmed(state, 3, 1U));
}

void test_mqtt_event_is_live() {
    const void *client = reinterpret_cast<const void *>(0x100);
    const void *other = reinterpret_cast<const void *>(0x200);
    TEST_ASSERT_TRUE(mqttEventIsLive(client, client, 3U, 3U));
    TEST_ASSERT_FALSE(mqttEventIsLive(nullptr, client, 3U, 3U));
    TEST_ASSERT_FALSE(mqttEventIsLive(client, nullptr, 3U, 3U));
    TEST_ASSERT_FALSE(mqttEventIsLive(client, other, 3U, 3U));
    TEST_ASSERT_FALSE(mqttEventIsLive(client, client, 3U, 4U));
}

void test_publish_ack_reserve_attach_survives_async_abort() {
    MqttPublishAckState state{};
    TEST_ASSERT_TRUE(mqttPublishAckBeginAllowed(mqttPublishAckCanBegin(state), true));
    TEST_ASSERT_TRUE(mqttPublishAckReserve(&state, 3U, 42));
    TEST_ASSERT_TRUE(mqttPublishAckIsStarting(state));
    TEST_ASSERT_TRUE(mqttPublishAckBlocksNewPublish(state));
    TEST_ASSERT_FALSE(mqttPublishAckCanBegin(state));
    TEST_ASSERT_FALSE(mqttPublishAckIsPending(state));
    TEST_ASSERT_FALSE(mqttPublishAckTimeoutDue(mqttPublishAckIsPending(state), true, 0UL, 5000UL, 5000UL));
    TEST_ASSERT_FALSE(mqttPublishAckReserve(&state, 3U, 43));
    TEST_ASSERT_FALSE(mqttPublishAckBegin(&state, 8, 3U, 43));
    TEST_ASSERT_FALSE(mqttPublishAckFailIfPending(&state, 3U));
    TEST_ASSERT_TRUE(mqttPublishAckIsStarting(state));
    // Abort without ACK fails async (BeginAllowed false) but Attach must still bind.
    TEST_ASSERT_FALSE(mqttPublishAckBeginAllowed(mqttPublishAckCanBegin(state), false));
    TEST_ASSERT_TRUE(mqttPublishAckAttach(&state, 7, 3U));
    TEST_ASSERT_TRUE(mqttPublishAckIsPending(state));
    TEST_ASSERT_EQUAL_INT(7, state.messageId);
    TEST_ASSERT_EQUAL_INT(42, state.expectedCounter);
    TEST_ASSERT_TRUE(mqttPublishAckConfirm(&state, 7, 3U));
    TEST_ASSERT_TRUE(mqttPublishAckWasConfirmed(state, 7, 3U));
}

void test_publish_ack_late_puback_during_starting() {
    MqttPublishAckState state{};
    TEST_ASSERT_TRUE(mqttPublishAckReserve(&state, 3U, 11));
    TEST_ASSERT_FALSE(mqttPublishAckConfirm(&state, 7, 3U));
    TEST_ASSERT_TRUE(mqttPublishAckIsStarting(state));
    TEST_ASSERT_FALSE(mqttPublishAckWasConfirmed(state, 7, 3U));
    TEST_ASSERT_TRUE(mqttPublishAckAttach(&state, 7, 3U));
    TEST_ASSERT_TRUE(mqttPublishAckWasConfirmed(state, 7, 3U));
    TEST_ASSERT_FALSE(mqttPublishAckIsPending(state));
}

void test_publish_ack_reserved_survives_idle_reset() {
    MqttPublishAckState starting{};
    TEST_ASSERT_TRUE(mqttPublishAckReserve(&starting, 1U, 4));
    TEST_ASSERT_TRUE(mqttPublishAckIsReserved(starting));
    TEST_ASSERT_FALSE(mqttPublishAckIsPending(starting));
    MqttPublishAckState pending{};
    TEST_ASSERT_TRUE(mqttPublishAckBegin(&pending, 2, 1U, 5));
    TEST_ASSERT_TRUE(mqttPublishAckIsReserved(pending));
    MqttPublishAckState idle{};
    TEST_ASSERT_FALSE(mqttPublishAckIsReserved(idle));
    MqttPublishAckState acked{};
    TEST_ASSERT_TRUE(mqttPublishAckBegin(&acked, 3, 1U, 6));
    TEST_ASSERT_TRUE(mqttPublishAckConfirm(&acked, 3, 1U));
    TEST_ASSERT_FALSE(mqttPublishAckIsReserved(acked));
}

void test_publish_ack_reserve_abort_blocks_attach() {
    MqttPublishAckState state{};
    TEST_ASSERT_TRUE(mqttPublishAckReserve(&state, 2U, 9));
    TEST_ASSERT_TRUE(mqttPublishAckFail(&state, 2U));
    TEST_ASSERT_FALSE(mqttPublishAckIsStarting(state));
    TEST_ASSERT_TRUE(mqttPublishAckCanBegin(state));
    TEST_ASSERT_FALSE(mqttPublishAckAttach(&state, 4, 2U));
    TEST_ASSERT_FALSE(mqttPublishAckConfirm(&state, 4, 2U));
}

void test_publish_ack_timeout_gen0() {
    MqttPublishAckState state{};
    TEST_ASSERT_TRUE(mqttPublishAckBegin(&state, 11, 0U, 7));
    TEST_ASSERT_TRUE(mqttPublishAckIsPending(state));
    TEST_ASSERT_FALSE(mqttPublishAckTimeoutDue(true, false, 0UL, 5000UL, 5000UL));
    TEST_ASSERT_FALSE(mqttPublishAckTimeoutDue(false, true, 0UL, 5000UL, 5000UL));
    TEST_ASSERT_FALSE(mqttPublishAckTimeoutDue(true, true, 0UL, 4999UL, 5000UL));
    TEST_ASSERT_TRUE(mqttPublishAckTimeoutDue(true, true, 0UL, 5000UL, 5000UL));
    TEST_ASSERT_TRUE(mqttPublishAckFail(&state, 0U));
    TEST_ASSERT_FALSE(mqttPublishAckIsPending(state));
    TEST_ASSERT_FALSE(mqttPublishAckTimeoutDue(mqttPublishAckIsPending(state), true, 0UL, 5000UL, 5000UL));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_mqtt_topic_syntax);
    RUN_TEST(test_mqtt_server_syntax);
    RUN_TEST(test_mqtt_username_syntax);
    RUN_TEST(test_mqtt_password_syntax);
    RUN_TEST(test_device_id_syntax);
    RUN_TEST(test_device_id_create_mode);
    RUN_TEST(test_had_prior_mqtt_setup_keys);
    RUN_TEST(test_mqtt_chaya_publish_try_is_fail);
    RUN_TEST(test_device_id_format_from_bytes);
    RUN_TEST(test_device_sta_hostname_format);
    RUN_TEST(test_normalize_mqtt_port);
    RUN_TEST(test_pairing_topics);
    RUN_TEST(test_sanitize_partner_and_server);
    RUN_TEST(test_counter_payload_parse);
    RUN_TEST(test_backoff_helpers);
    RUN_TEST(test_publish_ack_state);
    RUN_TEST(test_publish_ack_begin_blocked_after_confirm);
    RUN_TEST(test_mqtt_pack_roundtrip);
    RUN_TEST(test_mqtt_pack_reject_bad_magic);
    RUN_TEST(test_mqtt_settings_apply_clear_pending);
    RUN_TEST(test_publish_ack_begin_blocked_when_async_not_pending);
    RUN_TEST(test_abort_may_fail_async);
    RUN_TEST(test_mqtt_event_is_live);
    RUN_TEST(test_publish_ack_reserve_attach_survives_async_abort);
    RUN_TEST(test_publish_ack_late_puback_during_starting);
    RUN_TEST(test_publish_ack_reserved_survives_idle_reset);
    RUN_TEST(test_publish_ack_reserve_abort_blocks_attach);
    RUN_TEST(test_publish_ack_timeout_gen0);
    return UNITY_END();
}
