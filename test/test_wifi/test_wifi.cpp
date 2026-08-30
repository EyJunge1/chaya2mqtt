#include <cstring>
#include <unity.h>

#include "constants.h"
#include "util/net_validate.h"
#include "wifi/wifi_qr_pure.h"
#include "wifi/wlan_config.h"
#include "wifi/wlan_pack.h"
#include "wifi/wlan_recovery.h"
#include "wifi/wlan_soft_reconnect.h"

void test_setup_ap_pass_syntax_and_format() {
    TEST_ASSERT_FALSE(setupApPassSyntaxOk("00000000")); // legacy 8-digit rejected
    TEST_ASSERT_FALSE(setupApPassSyntaxOk("1234567"));
    TEST_ASSERT_FALSE(setupApPassSyntaxOk(""));
    TEST_ASSERT_FALSE(setupApPassSyntaxOk(nullptr));
    TEST_ASSERT_FALSE(setupApPassSyntaxOk("short"));
    TEST_ASSERT_TRUE(setupApPassSyntaxOk("ABCDEFGHIJKLMNOPQRSTUVWX"));
    TEST_ASSERT_TRUE(setupApPassSyntaxOk("abcdefghijklmnopqrstuvwx"));
    TEST_ASSERT_TRUE(setupApPassSyntaxOk("0123456789ABCDEFGHIJKLMN"));
    TEST_ASSERT_FALSE(setupApPassSyntaxOk("ABCDEFGHIJKLMNOPQRSTUVW!"));

    char pin[kSetupApPassBufLen]{};
    uint8_t rnd[kSetupApPassLen]{};
    for (size_t i = 0; i < kSetupApPassLen; ++i) {
        rnd[i] = static_cast<uint8_t>(i);
    }
    TEST_ASSERT_TRUE(formatSetupApPassFromRandom(rnd, sizeof(rnd), pin, sizeof(pin)));
    TEST_ASSERT_TRUE(setupApPassSyntaxOk(pin));
    TEST_ASSERT_FALSE(formatSetupApPassFromRandom(rnd, 4U, pin, sizeof(pin)));
    TEST_ASSERT_FALSE(formatSetupApPassFromRandom(rnd, sizeof(rnd), pin, 8U));
}

void test_wlan_boot_decision_keeps_configured_device_out_of_setup_ap() {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WlanBootAction::StartSetupAp),
                          static_cast<int>(wlanBootDecide(false, false, false)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WlanBootAction::WaitForSta),
                          static_cast<int>(wlanBootDecide(true, false, false)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WlanBootAction::FinishSta),
                          static_cast<int>(wlanBootDecide(true, true, false)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WlanBootAction::ContinueStaOnly),
                          static_cast<int>(wlanBootDecide(true, false, true)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WlanBootAction::FinishSta),
                          static_cast<int>(wlanBootDecide(true, true, true)));
}

void test_wifi_qr_payload() {
    char out[kWifiQrPayloadMaxLen]{};
    TEST_ASSERT_TRUE(wifiQrBuildWpaPayload("Chaya2MQTT", "12345678", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("WIFI:T:WPA;S:Chaya2MQTT;P:12345678;;", out);

    TEST_ASSERT_TRUE(wifiQrBuildWpaPayload("A;B", "x:y", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("WIFI:T:WPA;S:A\\;B;P:x\\:y;;", out);

    TEST_ASSERT_TRUE(wifiQrBuildWpaPayload("Home", "", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("WIFI:T:WPA;S:Home;P:;;", out);

    TEST_ASSERT_FALSE(wifiQrBuildWpaPayload("", "12345678", out, sizeof(out)));
    TEST_ASSERT_FALSE(wifiQrBuildWpaPayload(nullptr, "12345678", out, sizeof(out)));
    TEST_ASSERT_FALSE(wifiQrBuildWpaPayload("Home", "12345678", nullptr, 8));
    TEST_ASSERT_FALSE(wifiQrBuildWpaPayload("Home", "12345678", out, 8));

    char esc[16]{};
    TEST_ASSERT_EQUAL_UINT(4, wifiQrEscapeField("a;b", esc, sizeof(esc)));
    TEST_ASSERT_EQUAL_STRING("a\\;b", esc);
}

void test_wifi_ssid_syntax() {
    TEST_ASSERT_TRUE(wifiSsidSyntaxOk("MyNetwork", 33));
    TEST_ASSERT_FALSE(wifiSsidSyntaxOk("bad\x01", 33));
    TEST_ASSERT_FALSE(wifiSsidSyntaxOk("", 33));
}

void test_ipv4_and_netmask_validation() {
    uint8_t ip[4]{};
    TEST_ASSERT_TRUE(parseIpv4Dotted("192.168.1.10", ip));
    TEST_ASSERT_EQUAL_UINT8(192, ip[0]);
    TEST_ASSERT_EQUAL_UINT8(168, ip[1]);
    TEST_ASSERT_EQUAL_UINT8(1, ip[2]);
    TEST_ASSERT_EQUAL_UINT8(10, ip[3]);
    TEST_ASSERT_FALSE(parseIpv4Dotted("192.168.01.10", ip));
    TEST_ASSERT_FALSE(parseIpv4Dotted("192.168.1", ip));
    TEST_ASSERT_FALSE(parseIpv4Dotted("256.0.0.1", ip));

    uint8_t mask[4]{};
    TEST_ASSERT_TRUE(parseIpv4Dotted("255.255.255.0", mask));
    TEST_ASSERT_TRUE(ipv4NetmaskContiguousOk(mask));
    TEST_ASSERT_TRUE(parseIpv4Dotted("255.255.0.0", mask));
    TEST_ASSERT_TRUE(ipv4NetmaskContiguousOk(mask));
    TEST_ASSERT_TRUE(parseIpv4Dotted("255.255.255.128", mask));
    TEST_ASSERT_TRUE(ipv4NetmaskContiguousOk(mask));
    TEST_ASSERT_TRUE(parseIpv4Dotted("255.255.0.255", mask));
    TEST_ASSERT_FALSE(ipv4NetmaskContiguousOk(mask));
    TEST_ASSERT_TRUE(parseIpv4Dotted("0.0.0.0", mask));
    TEST_ASSERT_FALSE(ipv4NetmaskContiguousOk(mask));

    uint8_t a[4]{};
    uint8_t b[4]{};
    uint8_t m[4]{};
    TEST_ASSERT_TRUE(parseIpv4Dotted("192.168.1.50", a));
    TEST_ASSERT_TRUE(parseIpv4Dotted("192.168.1.1", b));
    TEST_ASSERT_TRUE(parseIpv4Dotted("255.255.255.0", m));
    TEST_ASSERT_TRUE(ipv4SameSubnet(a, b, m));
    TEST_ASSERT_TRUE(parseIpv4Dotted("192.168.2.1", b));
    TEST_ASSERT_FALSE(ipv4SameSubnet(a, b, m));

    TEST_ASSERT_TRUE(ntpHostSyntaxOk("pool.ntp.org", 64));
    TEST_ASSERT_TRUE(ntpHostSyntaxOk("192.168.1.1", 64));
    TEST_ASSERT_FALSE(ntpHostSyntaxOk("", 64));
    TEST_ASSERT_FALSE(ntpHostSyntaxOk("bad host", 64));
}

void test_wlan_config_validate() {
    WlanConfig cfg{};
    wlanConfigClear(&cfg);
    wlanConfigCopyStr(cfg.ssid, sizeof(cfg.ssid), "Home");
    cfg.mode = WlanIpMode::Dhcp;
    TEST_ASSERT_NULL(wlanConfigValidate(&cfg));
    wlanConfigSetNtpDefaults(&cfg);
    TEST_ASSERT_NULL(wlanConfigValidate(&cfg));

    cfg.mode = WlanIpMode::Static;
    TEST_ASSERT_EQUAL_STRING("ip", wlanConfigValidate(&cfg));
    wlanConfigCopyStr(cfg.ip, sizeof(cfg.ip), "192.168.1.50");
    wlanConfigCopyStr(cfg.gateway, sizeof(cfg.gateway), "192.168.1.1");
    wlanConfigCopyStr(cfg.netmask, sizeof(cfg.netmask), "255.255.255.0");
    TEST_ASSERT_NULL(wlanConfigValidate(&cfg));

    wlanConfigCopyStr(cfg.dns1, sizeof(cfg.dns1), "1.1.1.1");
    TEST_ASSERT_NULL(wlanConfigValidate(&cfg));
    wlanConfigCopyStr(cfg.dns1, sizeof(cfg.dns1), "0.0.0.0");
    TEST_ASSERT_EQUAL_STRING("dns1", wlanConfigValidate(&cfg));
    cfg.dns1[0] = '\0';

    cfg.mode = WlanIpMode::Dhcp;
    wlanConfigCopyStr(cfg.dns1, sizeof(cfg.dns1), "8.8.8.8");
    wlanConfigCopyStr(cfg.dns2, sizeof(cfg.dns2), "1.1.1.1");
    TEST_ASSERT_NULL(wlanConfigValidate(&cfg));

    cfg.mode = WlanIpMode::Static;
    wlanConfigCopyStr(cfg.ip, sizeof(cfg.ip), "192.168.1.50");
    wlanConfigCopyStr(cfg.gateway, sizeof(cfg.gateway), "10.0.0.1");
    wlanConfigCopyStr(cfg.netmask, sizeof(cfg.netmask), "255.255.255.0");
    TEST_ASSERT_EQUAL_STRING("subnet", wlanConfigValidate(&cfg));
    wlanConfigCopyStr(cfg.gateway, sizeof(cfg.gateway), "192.168.1.1");
    wlanConfigCopyStr(cfg.netmask, sizeof(cfg.netmask), "255.255.0.255");
    TEST_ASSERT_EQUAL_STRING("netmask", wlanConfigValidate(&cfg));
}

void test_wlan_pack_roundtrip() {
    WlanConfig cfg{};
    wlanConfigClear(&cfg);
    wlanConfigCopyStr(cfg.ssid, sizeof(cfg.ssid), "Home");
    wlanConfigCopyStr(cfg.pass, sizeof(cfg.pass), "secret");
    cfg.mode = WlanIpMode::Static;
    wlanConfigCopyStr(cfg.ip, sizeof(cfg.ip), "192.168.1.50");
    wlanConfigCopyStr(cfg.gateway, sizeof(cfg.gateway), "192.168.1.1");
    wlanConfigCopyStr(cfg.netmask, sizeof(cfg.netmask), "255.255.255.0");
    wlanConfigCopyStr(cfg.dns1, sizeof(cfg.dns1), "1.1.1.1");

    PackedWifiConfigV2 pk{};
    wlanPackConfigV2(cfg, &pk);
    TEST_ASSERT_EQUAL_UINT32(kWifiCfgPackedMagic, pk.magic);

    WlanConfig out{};
    TEST_ASSERT_TRUE(wlanUnpackConfigV2(pk, &out));
    TEST_ASSERT_EQUAL_STRING("Home", out.ssid);
    TEST_ASSERT_EQUAL_STRING("secret", out.pass);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WlanIpMode::Static), static_cast<int>(out.mode));
    TEST_ASSERT_EQUAL_STRING("192.168.1.50", out.ip);
    TEST_ASSERT_EQUAL_STRING("192.168.1.1", out.gateway);
    TEST_ASSERT_EQUAL_STRING("255.255.255.0", out.netmask);
    TEST_ASSERT_EQUAL_STRING("1.1.1.1", out.dns1);

    pk.magic = 0;
    TEST_ASSERT_FALSE(wlanUnpackConfigV2(pk, &out));
}

void test_wlan_recovery_decide() {
    WlanRecoveryState st{};
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WlanRecoveryAction::None),
                          static_cast<int>(wlanRecoveryDecide(true, false, false, true, 1000, 1000, st)));

    st = {};
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WlanRecoveryAction::None),
                          static_cast<int>(wlanRecoveryDecide(false, true, false, true, 1000, 1000, st)));
    TEST_ASSERT_EQUAL_UINT32(0UL, st.linkDownSinceMs);

    st = {};
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WlanRecoveryAction::None),
                          static_cast<int>(wlanRecoveryDecide(false, false, false, true, 1000, 1000, st)));
    TEST_ASSERT_TRUE(st.linkDownSinceMs != 0UL);

    const unsigned long downStart = st.linkDownSinceMs;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(WlanRecoveryAction::None),
        static_cast<int>(wlanRecoveryDecide(false, false, false, true, downStart + 1000UL, 1000UL, st)));

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(WlanRecoveryAction::ForcedReassoc),
        static_cast<int>(wlanRecoveryDecide(false, false, false, true,
                                            downStart + kWlanRecoveryLinkDownGraceMs, 1000UL, st)));

    // Cooldown prevents immediate second reassoc.
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(WlanRecoveryAction::None),
        static_cast<int>(wlanRecoveryDecide(false, false, false, true,
                                            downStart + kWlanRecoveryLinkDownGraceMs + 1000UL, 1000UL,
                                            st)));

    // Restart requires long outage + min uptime; blocked during OTA.
    st.linkDownSinceMs = 1UL;
    st.lastForcedReassocMs = 1UL;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(WlanRecoveryAction::None),
        static_cast<int>(wlanRecoveryDecide(false, false, true, true, kWlanRecoveryRestartAfterMs + 10UL,
                                            kWlanRecoveryMinUptimeBeforeRestartMs + 10UL, st)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(WlanRecoveryAction::Restart),
        static_cast<int>(wlanRecoveryDecide(false, false, false, true, kWlanRecoveryRestartAfterMs + 10UL,
                                            kWlanRecoveryMinUptimeBeforeRestartMs + 10UL, st)));
    // Cap restarts → ForcedReassoc instead (STAB-03).
    st.linkDownSinceMs = 1UL;
    st.lastForcedReassocMs = 0UL;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(WlanRecoveryAction::ForcedReassoc),
        static_cast<int>(wlanRecoveryDecide(false, false, false, true, kWlanRecoveryRestartAfterMs + 10UL,
                                            kWlanRecoveryMinUptimeBeforeRestartMs + 10UL, st,
                                            kWlanRecoveryMaxRestartsPerDay,
                                            kWlanRecoveryMaxRestartsPerDay)));
}

void test_wifi_soft_reconnect_escalation_threshold() {
    TEST_ASSERT_EQUAL_UINT32(2U, kWifiSoftReconnectAttemptsBeforeForce);
    TEST_ASSERT_TRUE(kWifiSoftReconnectAttemptsBeforeForce > 0U);
    TEST_ASSERT_FALSE(wlanSoftReconnectShouldForce(0U, kWifiSoftReconnectAttemptsBeforeForce));
    TEST_ASSERT_FALSE(wlanSoftReconnectShouldForce(1U, kWifiSoftReconnectAttemptsBeforeForce));
    TEST_ASSERT_TRUE(wlanSoftReconnectShouldForce(2U, kWifiSoftReconnectAttemptsBeforeForce));
    TEST_ASSERT_TRUE(wlanSoftReconnectShouldForce(5U, kWifiSoftReconnectAttemptsBeforeForce));
    // Soft→Force threshold orchestration: failCount climbs then escalates (TEST-06).
    uint32_t fails = 0;
    while (!wlanSoftReconnectShouldForce(fails, kWifiSoftReconnectAttemptsBeforeForce)) {
        ++fails;
        TEST_ASSERT_TRUE(fails <= kWifiSoftReconnectAttemptsBeforeForce);
    }
    TEST_ASSERT_EQUAL_UINT32(kWifiSoftReconnectAttemptsBeforeForce, fails);
}

void test_wlan_epd_tx_power_from_rssi() {
    constexpr int8_t kCur = 52; // 13 dBm configured max
    TEST_ASSERT_EQUAL_INT8(kWifiEpdTxPowerStrongQdbm,
                           wlanEpdTxPowerQuarterDbmFromRssi(-55, kCur));
    TEST_ASSERT_EQUAL_INT8(kWifiEpdTxPowerStrongQdbm,
                           wlanEpdTxPowerQuarterDbmFromRssi(-40, kCur));
    TEST_ASSERT_EQUAL_INT8(kWifiEpdTxPowerMediumQdbm,
                           wlanEpdTxPowerQuarterDbmFromRssi(-56, kCur));
    TEST_ASSERT_EQUAL_INT8(kWifiEpdTxPowerMediumQdbm,
                           wlanEpdTxPowerQuarterDbmFromRssi(-64, kCur));
    TEST_ASSERT_EQUAL_INT8(kWifiEpdTxPowerWeakQdbm,
                           wlanEpdTxPowerQuarterDbmFromRssi(-65, kCur));
    TEST_ASSERT_EQUAL_INT8(kWifiEpdTxPowerWeakQdbm,
                           wlanEpdTxPowerQuarterDbmFromRssi(-90, kCur));
    // Unknown / not associated
    TEST_ASSERT_EQUAL_INT8(kWifiEpdTxPowerWeakQdbm,
                           wlanEpdTxPowerQuarterDbmFromRssi(0, kCur));
    TEST_ASSERT_EQUAL_INT8(kWifiEpdTxPowerWeakQdbm,
                           wlanEpdTxPowerQuarterDbmFromRssi(1, kCur));
    // Never raise above the current max
    TEST_ASSERT_EQUAL_INT8(6, wlanEpdTxPowerQuarterDbmFromRssi(-90, 6));
    TEST_ASSERT_EQUAL_INT8(20, wlanEpdTxPowerQuarterDbmFromRssi(-60, 20));
    TEST_ASSERT_EQUAL_INT8(8, wlanEpdTxPowerQuarterDbmFromRssi(-40, 8));
}

void test_wlan_unpack_invalid_static_falls_back_dhcp() {
    WlanConfig cfg{};
    wlanConfigClear(&cfg);
    wlanConfigCopyStr(cfg.ssid, sizeof(cfg.ssid), "Home");
    cfg.mode = WlanIpMode::Static;
    wlanConfigCopyStr(cfg.ip, sizeof(cfg.ip), "192.168.1.50");
    wlanConfigCopyStr(cfg.gateway, sizeof(cfg.gateway), "10.0.0.1");
    wlanConfigCopyStr(cfg.netmask, sizeof(cfg.netmask), "255.255.255.0");

    PackedWifiConfigV2 pk{};
    wlanPackConfigV2(cfg, &pk);
    WlanConfig out{};
    TEST_ASSERT_TRUE(wlanUnpackConfigV2(pk, &out));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WlanIpMode::Dhcp), static_cast<int>(out.mode));
    TEST_ASSERT_EQUAL_STRING("", out.ip);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_setup_ap_pass_syntax_and_format);
    RUN_TEST(test_wlan_boot_decision_keeps_configured_device_out_of_setup_ap);
    RUN_TEST(test_wifi_qr_payload);
    RUN_TEST(test_wifi_ssid_syntax);
    RUN_TEST(test_ipv4_and_netmask_validation);
    RUN_TEST(test_wlan_config_validate);
    RUN_TEST(test_wlan_pack_roundtrip);
    RUN_TEST(test_wlan_recovery_decide);
    RUN_TEST(test_wifi_soft_reconnect_escalation_threshold);
    RUN_TEST(test_wlan_epd_tx_power_from_rssi);
    RUN_TEST(test_wlan_unpack_invalid_static_falls_back_dhcp);
    return UNITY_END();
}
