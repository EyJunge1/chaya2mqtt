#include <unity.h>

#include "device_runtime.h"

void test_sim_first_connect_and_pair() {
    DeviceRuntime dev("a1b2c3");
    dev.configureBroker("broker.example.com", 8883, "u", "p");
    TEST_ASSERT_TRUE(dev.pair("f5e6d7"));
    TEST_ASSERT_EQUAL_STRING("chaya2mqtt/a1b2c3", dev.mqtt().topicPub);
    TEST_ASSERT_EQUAL_STRING("chaya2mqtt/f5e6d7", dev.mqtt().topicSub);

    dev.net().setReadyForMqtt();
    dev.tick(1000);
    TEST_ASSERT_TRUE(dev.mqttConnected());
    TEST_ASSERT_EQUAL_UINT32(1U, static_cast<unsigned>(dev.transport().subscribeLog.size()));
    TEST_ASSERT_EQUAL_STRING("chaya2mqtt/f5e6d7", dev.transport().subscribeLog[0].c_str());
}

void test_sim_disconnect_reconnect_backoff() {
    DeviceRuntime dev("a1b2c3");
    dev.configureBroker("broker.example.com", 8883, "", "");
    dev.net().setReadyForMqtt();
    dev.tick(0);
    TEST_ASSERT_TRUE(dev.mqttConnected());

    dev.forceDisconnect(false);
    TEST_ASSERT_FALSE(dev.mqttConnected());
    dev.tick(1000);
    TEST_ASSERT_FALSE(dev.mqttConnected());
    dev.tick(dev.backoff().lastAttemptAtMs + dev.backoff().backoffPeriodMs);
    TEST_ASSERT_TRUE(dev.mqttConnected());
}

void test_sim_invalid_and_changed_broker() {
    DeviceRuntime dev("a1b2c3");
    dev.configureBroker("bad host", 0, "", "");
    TEST_ASSERT_EQUAL_STRING("", dev.mqtt().server);
    TEST_ASSERT_EQUAL_UINT16(8883, dev.mqtt().port);

    dev.configureBroker("ok.example.com", 1883, "", "");
    TEST_ASSERT_EQUAL_STRING("ok.example.com", dev.mqtt().server);
    TEST_ASSERT_EQUAL_UINT16(1883, dev.mqtt().port);
}

void test_sim_unpair_and_publish() {
    DeviceRuntime dev("a1b2c3");
    dev.configureBroker("broker.example.com", 8883, "", "");
    TEST_ASSERT_TRUE(dev.pair("f5e6d7"));
    dev.unpair();
    TEST_ASSERT_EQUAL_STRING("", dev.mqtt().partnerDeviceId);
    TEST_ASSERT_EQUAL_STRING("", dev.mqtt().topicSub);

    dev.net().setReadyForMqtt();
    dev.tick(10);
    TEST_ASSERT_TRUE(dev.publishCounter(7));
    TEST_ASSERT_TRUE(dev.publishPending());
    TEST_ASSERT_EQUAL_INT(0, dev.localTxCounter());
    TEST_ASSERT_FALSE(dev.publishCounter(7));
    TEST_ASSERT_TRUE(dev.confirmPendingPublish());
    TEST_ASSERT_EQUAL_INT(1, dev.localTxCounter());
    TEST_ASSERT_EQUAL_UINT32(1U, static_cast<unsigned>(dev.transport().publishLog.size()));
    TEST_ASSERT_EQUAL_STRING("7", dev.transport().publishLog[0].payload.c_str());
}

void test_sim_publish_failure_and_remote_inject() {
    DeviceRuntime dev("a1b2c3");
    dev.configureBroker("broker.example.com", 8883, "", "");
    TEST_ASSERT_TRUE(dev.pair("f5e6d7"));
    dev.net().setReadyForMqtt();
    dev.tick(1);
    dev.transport().failNextPublish = true;
    TEST_ASSERT_FALSE(dev.publishCounter(1));
    TEST_ASSERT_TRUE(dev.injectRemoteCounter("99"));
    TEST_ASSERT_EQUAL_INT(99, static_cast<int>(dev.remoteCounter()));
    TEST_ASSERT_FALSE(dev.injectRemoteCounter("x"));
}

void test_sim_nvs_restart_recovery() {
    DeviceRuntime dev("a1b2c3");
    dev.configureBroker("broker.example.com", 8883, "user", "pass");
    TEST_ASSERT_TRUE(dev.pair("f5e6d7"));
    TEST_ASSERT_TRUE(dev.persist());

    DeviceRuntime restarted("a1b2c3");
    restarted.nvs() = dev.nvs();
    TEST_ASSERT_TRUE(restarted.restore());
    TEST_ASSERT_EQUAL_STRING("broker.example.com", restarted.mqtt().server);
    TEST_ASSERT_EQUAL_STRING("f5e6d7", restarted.mqtt().partnerDeviceId);
    TEST_ASSERT_EQUAL_STRING("chaya2mqtt/a1b2c3", restarted.mqtt().topicPub);
}

void test_sim_wifi_down_defers_connect() {
    DeviceRuntime dev("a1b2c3");
    dev.configureBroker("broker.example.com", 8883, "", "");
    dev.net().setOffline();
    dev.tick(0);
    TEST_ASSERT_FALSE(dev.mqttConnected());
    TEST_ASSERT_EQUAL_UINT32(kMqttWifiDownBackoffMs, dev.backoff().backoffPeriodMs);

    dev.net().setReadyForMqtt();
    dev.tick(kMqttWifiDownBackoffMs);
    TEST_ASSERT_TRUE(dev.mqttConnected());
}

void test_sim_broker_outage_while_wifi_stable() {
    DeviceRuntime dev("a1b2c3");
    dev.configureBroker("broker.example.com", 8883, "", "");
    TEST_ASSERT_TRUE(dev.pair("f5e6d7"));
    dev.net().setReadyForMqtt();
    dev.tick(0);
    TEST_ASSERT_TRUE(dev.mqttConnected());

    // Broker drops while Wi‑Fi/NTP remain ready — MQTT backoff, then reconnect.
    dev.forceDisconnect(false);
    TEST_ASSERT_FALSE(dev.mqttConnected());
    TEST_ASSERT_TRUE(dev.net().wifiConnected);
    dev.tick(1000);
    TEST_ASSERT_FALSE(dev.mqttConnected());
    const unsigned long wait = dev.backoff().backoffPeriodMs;
    TEST_ASSERT_TRUE(wait >= kMqttBackoffInitialMs);
    dev.tick(dev.backoff().lastAttemptAtMs + wait);
    TEST_ASSERT_TRUE(dev.mqttConnected());
}

void test_sim_wifi_config_nvs_roundtrip() {
    DeviceRuntime dev;
    WlanConfig cfg{};
    wlanConfigClear(&cfg);
    wlanConfigCopyStr(cfg.ssid, sizeof(cfg.ssid), "Home");
    wlanConfigCopyStr(cfg.pass, sizeof(cfg.pass), "secret");
    cfg.mode = WlanIpMode::Dhcp;
    TEST_ASSERT_TRUE(dev.nvs().saveWifi(cfg));

    WlanConfig loaded{};
    TEST_ASSERT_TRUE(dev.nvs().loadWifi(&loaded));
    TEST_ASSERT_EQUAL_STRING("Home", loaded.ssid);
    TEST_ASSERT_EQUAL_STRING("secret", loaded.pass);
}

void test_sim_connect_failure_backoff_grows() {
    DeviceRuntime dev("a1b2c3");
    dev.configureBroker("broker.example.com", 8883, "", "");
    dev.net().setReadyForMqtt();
    dev.transport().failNextConnect = true;
    dev.tick(0);
    TEST_ASSERT_FALSE(dev.mqttConnected());
    const unsigned long first = dev.backoff().backoffPeriodMs;
    TEST_ASSERT_TRUE(first >= kMqttBackoffInitialMs);

    dev.advance(first);
    TEST_ASSERT_TRUE(dev.mqttConnected());
}

void test_sim_ntp_not_ready_defers() {
    DeviceRuntime dev("a1b2c3");
    dev.configureBroker("broker.example.com", 8883, "", "");
    dev.net().setWifiUpUnstable();
    dev.tick(0);
    TEST_ASSERT_FALSE(dev.mqttConnected());
    TEST_ASSERT_EQUAL_UINT32(kMqttNtpRetryMs, dev.backoff().backoffPeriodMs);

    dev.net().setReadyForMqtt();
    dev.advance(kMqttNtpRetryMs);
    TEST_ASSERT_TRUE(dev.mqttConnected());
}

void test_sim_nvs_save_failure_and_counter_path() {
    DeviceRuntime dev("a1b2c3");
    dev.configureBroker("broker.example.com", 8883, "", "");
    TEST_ASSERT_TRUE(dev.pair("f5e6d7"));
    dev.nvs().failNextMqttSave = true;
    TEST_ASSERT_FALSE(dev.persist());
    TEST_ASSERT_TRUE(dev.persist());

    dev.net().setReadyForMqtt();
    dev.tick(5);
    TEST_ASSERT_TRUE(dev.publishCounter(3));
    TEST_ASSERT_EQUAL_INT(0, dev.localTxCounter());
    TEST_ASSERT_TRUE(dev.confirmPendingPublish());
    TEST_ASSERT_EQUAL_INT(1, dev.localTxCounter());
    TEST_ASSERT_EQUAL_INT(1, dev.displayTxDelta());
    TEST_ASSERT_TRUE(dev.injectRemoteCounter("12"));
    TEST_ASSERT_EQUAL_INT(12, static_cast<int>(dev.remoteCounter()));
    TEST_ASSERT_EQUAL_INT(12, dev.displayRxDelta());
}

void test_sim_nvs_wifi_mqtt_fault_injection() {
    DeviceRuntime dev("a1b2c3");
    WlanConfig cfg{};
    wlanConfigClear(&cfg);
    wlanConfigCopyStr(cfg.ssid, sizeof(cfg.ssid), "Home");
    wlanConfigCopyStr(cfg.pass, sizeof(cfg.pass), "secret");
    cfg.mode = WlanIpMode::Dhcp;

    dev.nvs().failNextWifiSave = true;
    TEST_ASSERT_FALSE(dev.nvs().saveWifi(cfg));
    TEST_ASSERT_TRUE(dev.nvs().saveWifi(cfg));

    WlanConfig loaded{};
    dev.nvs().failNextWifiLoad = true;
    TEST_ASSERT_FALSE(dev.nvs().loadWifi(&loaded));
    TEST_ASSERT_TRUE(dev.nvs().loadWifi(&loaded));
    TEST_ASSERT_EQUAL_STRING("Home", loaded.ssid);

    TEST_ASSERT_TRUE(dev.pair("f5e6d7"));
    TEST_ASSERT_TRUE(dev.persist());
    MqttConfig mqtt{};
    dev.nvs().failNextMqttLoad = true;
    TEST_ASSERT_FALSE(dev.nvs().loadMqtt(&mqtt, "a1b2c3"));
    TEST_ASSERT_TRUE(dev.nvs().loadMqtt(&mqtt, "a1b2c3"));
    TEST_ASSERT_EQUAL_STRING("f5e6d7", mqtt.partnerDeviceId);
}

void test_sim_disconnect_aborts_pending_publish() {
    DeviceRuntime dev("a1b2c3");
    dev.configureBroker("broker.example.com", 8883, "", "");
    dev.net().setReadyForMqtt();
    dev.tick(0);
    TEST_ASSERT_TRUE(dev.publishCounter(1));
    const int messageId = dev.transport().publishLog.back().messageId;
    dev.forceDisconnect(false);
    TEST_ASSERT_FALSE(dev.publishPending());
    TEST_ASSERT_FALSE(dev.confirmPublish(messageId));
    TEST_ASSERT_EQUAL_INT(0, dev.localTxCounter());
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_sim_first_connect_and_pair);
    RUN_TEST(test_sim_disconnect_reconnect_backoff);
    RUN_TEST(test_sim_invalid_and_changed_broker);
    RUN_TEST(test_sim_unpair_and_publish);
    RUN_TEST(test_sim_publish_failure_and_remote_inject);
    RUN_TEST(test_sim_nvs_restart_recovery);
    RUN_TEST(test_sim_wifi_down_defers_connect);
    RUN_TEST(test_sim_broker_outage_while_wifi_stable);
    RUN_TEST(test_sim_wifi_config_nvs_roundtrip);
    RUN_TEST(test_sim_connect_failure_backoff_grows);
    RUN_TEST(test_sim_ntp_not_ready_defers);
    RUN_TEST(test_sim_nvs_save_failure_and_counter_path);
    RUN_TEST(test_sim_nvs_wifi_mqtt_fault_injection);
    RUN_TEST(test_sim_disconnect_aborts_pending_publish);
    return UNITY_END();
}
