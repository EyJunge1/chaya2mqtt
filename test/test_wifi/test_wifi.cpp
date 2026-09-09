#include <cstring>
#include <unity.h>

#include "async/system_shutdown_pure.h"
#include "config/nvs_blob_load_pure.h"
#include "config/nvs_write_gate_pure.h"
#include "constants.h"
#include "wifi/factory_wipe_pure.h"
#include "util/net_validate.h"
#include "wifi/wifi_qr_pure.h"
#include "wifi/test.h"
#include "wifi/wlan.h"
#include "wifi/wlan_config.h"
#include "wifi/wlan_event_pure.h"
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

    char psk[kSetupApPassBufLen]{};
    uint8_t rnd[kSetupApPassLen]{};
    for (size_t i = 0; i < kSetupApPassLen; ++i) {
        rnd[i] = static_cast<uint8_t>(i);
    }
    TEST_ASSERT_TRUE(formatSetupApPassFromRandom(rnd, sizeof(rnd), psk, sizeof(psk)));
    TEST_ASSERT_TRUE(setupApPassSyntaxOk(psk));
    TEST_ASSERT_FALSE(formatSetupApPassFromRandom(rnd, 4U, psk, sizeof(psk)));
    TEST_ASSERT_FALSE(formatSetupApPassFromRandom(rnd, sizeof(rnd), psk, 8U));
}

void test_setup_ap_pass_ensure_idempotent() {
    // Second Ensure after a RAM-only PSK must not generate again (BUG-NET-01).
    TEST_ASSERT_FALSE(setupApPassShouldGenerate(true, false));
    TEST_ASSERT_FALSE(setupApPassShouldGenerate(true, true));
    TEST_ASSERT_FALSE(setupApPassShouldGenerate(false, true));
    TEST_ASSERT_TRUE(setupApPassShouldGenerate(false, false));
}

void test_wlan_boot_decision_keeps_configured_device_out_of_setup_ap() {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WlanBootAction::StartSetupAp), static_cast<int>(wlanBootDecide(false, false, false)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WlanBootAction::WaitForSta), static_cast<int>(wlanBootDecide(true, false, false)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WlanBootAction::FinishSta), static_cast<int>(wlanBootDecide(true, true, false)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WlanBootAction::ContinueStaOnly), static_cast<int>(wlanBootDecide(true, false, true)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WlanBootAction::FinishSta), static_cast<int>(wlanBootDecide(true, true, true)));
}

void test_wlan_got_ip_decide_continue_sta_then_first_got_ip_runs_finish() {
    // ContinueStaOnly already cleared pending; first GOT_IP must still finish (NTP).
    const WlanGotIpActions a = wlanGotIpDecide(false, false, false);
    TEST_ASSERT_TRUE(a.runFinish);
    TEST_ASSERT_FALSE(a.markSettled);
    TEST_ASSERT_FALSE(a.playWifiUpIfNotFinish);
}

void test_wlan_got_ip_decide_second_got_ip_plays_wifi_up() {
    const WlanGotIpActions a = wlanGotIpDecide(true, false, true);
    TEST_ASSERT_FALSE(a.runFinish);
    TEST_ASSERT_FALSE(a.markSettled);
    TEST_ASSERT_TRUE(a.playWifiUpIfNotFinish);
}

void test_wlan_got_ip_decide_boot_pending_finish_and_settled() {
    const WlanGotIpActions a = wlanGotIpDecide(false, true, false);
    TEST_ASSERT_TRUE(a.runFinish);
    TEST_ASSERT_TRUE(a.markSettled);
    TEST_ASSERT_FALSE(a.playWifiUpIfNotFinish);
}

void test_wlan_got_ip_decide_finish_already_done_no_second_finish() {
    const WlanGotIpActions stillPending = wlanGotIpDecide(true, true, false);
    TEST_ASSERT_FALSE(stillPending.runFinish);
    TEST_ASSERT_TRUE(stillPending.markSettled);
    TEST_ASSERT_FALSE(stillPending.playWifiUpIfNotFinish);

    const WlanGotIpActions pendingCleared = wlanGotIpDecide(true, false, false);
    TEST_ASSERT_FALSE(pendingCleared.runFinish);
    TEST_ASSERT_FALSE(pendingCleared.playWifiUpIfNotFinish);
}

void test_wlan_got_ip_decide_status_finish_first_got_ip_no_second_wifi_up() {
    const WlanGotIpActions firstAfterStatusFinish = wlanGotIpDecide(true, false, false);
    TEST_ASSERT_FALSE(firstAfterStatusFinish.runFinish);
    TEST_ASSERT_FALSE(firstAfterStatusFinish.playWifiUpIfNotFinish);

    const WlanGotIpActions reconnect = wlanGotIpDecide(true, false, true);
    TEST_ASSERT_TRUE(reconnect.playWifiUpIfNotFinish);
}

void test_wlan_wifi_test_owns_radio() {
    TEST_ASSERT_FALSE(wlanWifiTestOwnsRadio(WlanWifiConnectionTestState::Idle));
    TEST_ASSERT_TRUE(wlanWifiTestOwnsRadio(WlanWifiConnectionTestState::Testing));
    TEST_ASSERT_TRUE(wlanWifiTestOwnsRadio(WlanWifiConnectionTestState::Ok));
    TEST_ASSERT_FALSE(wlanWifiTestOwnsRadio(WlanWifiConnectionTestState::Fail));
}

void test_wifi_qr_payload() {
    char out[kWifiQrPayloadMaxLen]{};
    TEST_ASSERT_TRUE(wifiQrBuildWpaPayload("Chaya2MQTT", "12345678", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("WIFI:T:WPA;S:Chaya2MQTT;P:12345678;;", out);

    // 24-char SoftAP PSK → ~52-byte MeCard. QR v3-M is 42 bytes; v4-M is 62.
    TEST_ASSERT_TRUE(wifiQrBuildWpaPayload("Chaya2MQTT", "ABCDEFGHIJKLMNOPQRSTUVWX", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("WIFI:T:WPA;S:Chaya2MQTT;P:ABCDEFGHIJKLMNOPQRSTUVWX;;", out);
    const size_t setupPayloadLen = std::strlen(out);
    TEST_ASSERT_EQUAL_UINT(52U, setupPayloadLen);
    TEST_ASSERT_TRUE(setupPayloadLen > 42U);
    TEST_ASSERT_TRUE(setupPayloadLen <= 62U);

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
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WlanRecoveryAction::None),
                          static_cast<int>(wlanRecoveryDecide(false, false, false, true, downStart + 1000UL, 1000UL, st)));

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(WlanRecoveryAction::ForcedReassoc),
        static_cast<int>(wlanRecoveryDecide(false, false, false, true, downStart + kWlanRecoveryLinkDownGraceMs, 1000UL, st)));

    // Cooldown prevents immediate second reassoc.
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WlanRecoveryAction::None),
                          static_cast<int>(wlanRecoveryDecide(false, false, false, true,
                                                              downStart + kWlanRecoveryLinkDownGraceMs + 1000UL, 1000UL, st)));

    // Restart requires long outage + min uptime; blocked during OTA.
    st.linkDownSinceMs = 1UL;
    st.lastForcedReassocMs = 1UL;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WlanRecoveryAction::None),
                          static_cast<int>(wlanRecoveryDecide(false, false, true, true, kWlanRecoveryRestartAfterMs + 10UL,
                                                              kWlanRecoveryMinUptimeBeforeRestartMs + 10UL, st)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WlanRecoveryAction::Restart),
                          static_cast<int>(wlanRecoveryDecide(false, false, false, true, kWlanRecoveryRestartAfterMs + 10UL,
                                                              kWlanRecoveryMinUptimeBeforeRestartMs + 10UL, st)));
    // Cap restarts → ForcedReassoc instead (STAB-03).
    st.linkDownSinceMs = 1UL;
    st.lastForcedReassocMs = 0UL;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WlanRecoveryAction::ForcedReassoc),
                          static_cast<int>(wlanRecoveryDecide(false, false, false, true, kWlanRecoveryRestartAfterMs + 10UL,
                                                              kWlanRecoveryMinUptimeBeforeRestartMs + 10UL, st,
                                                              kWlanRecoveryMaxRestartsPerDay, kWlanRecoveryMaxRestartsPerDay)));
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
    TEST_ASSERT_EQUAL_INT8(kWifiEpdTxPowerStrongQdbm, wlanEpdTxPowerQuarterDbmFromRssi(-55, kCur));
    TEST_ASSERT_EQUAL_INT8(kWifiEpdTxPowerStrongQdbm, wlanEpdTxPowerQuarterDbmFromRssi(-40, kCur));
    TEST_ASSERT_EQUAL_INT8(kWifiEpdTxPowerMediumQdbm, wlanEpdTxPowerQuarterDbmFromRssi(-56, kCur));
    TEST_ASSERT_EQUAL_INT8(kWifiEpdTxPowerMediumQdbm, wlanEpdTxPowerQuarterDbmFromRssi(-64, kCur));
    TEST_ASSERT_EQUAL_INT8(kWifiEpdTxPowerWeakQdbm, wlanEpdTxPowerQuarterDbmFromRssi(-65, kCur));
    TEST_ASSERT_EQUAL_INT8(kWifiEpdTxPowerWeakQdbm, wlanEpdTxPowerQuarterDbmFromRssi(-90, kCur));
    // Unknown / not associated
    TEST_ASSERT_EQUAL_INT8(kWifiEpdTxPowerWeakQdbm, wlanEpdTxPowerQuarterDbmFromRssi(0, kCur));
    TEST_ASSERT_EQUAL_INT8(kWifiEpdTxPowerWeakQdbm, wlanEpdTxPowerQuarterDbmFromRssi(1, kCur));
    // Never raise above the current max
    TEST_ASSERT_EQUAL_INT8(6, wlanEpdTxPowerQuarterDbmFromRssi(-90, 6));
    TEST_ASSERT_EQUAL_INT8(20, wlanEpdTxPowerQuarterDbmFromRssi(-60, 20));
    TEST_ASSERT_EQUAL_INT8(8, wlanEpdTxPowerQuarterDbmFromRssi(-40, 8));
}

void test_wlan_net_cmd_coalesce_enqueue() {
    TEST_ASSERT_TRUE(wlanNetCmdShouldEnqueue(false));
    TEST_ASSERT_FALSE(wlanNetCmdShouldEnqueue(true));
}

void test_wifi_scan_defer_while_ota() {
    TEST_ASSERT_FALSE(wifiScanServiceShouldDeferKick(false, false));
    TEST_ASSERT_TRUE(wifiScanServiceShouldDeferKick(true, false));
    TEST_ASSERT_TRUE(wifiScanServiceShouldDeferKick(false, true));
    TEST_ASSERT_TRUE(wifiScanServiceShouldDeferKick(true, true));
}

void test_wlan_scan_deadline_wrap() {
    TEST_ASSERT_FALSE(wlanMsBeforeDeadline(1000UL, 0UL));
    TEST_ASSERT_TRUE(wlanMsBeforeDeadline(1000UL, 1500UL));
    TEST_ASSERT_FALSE(wlanMsBeforeDeadline(1500UL, 1500UL));
    TEST_ASSERT_FALSE(wlanMsBeforeDeadline(1600UL, 1500UL));

    // Unsigned nowMs < nextAllowed stays true for ~49 days after millis wrap.
    const unsigned long nowAfterWrap = 10UL;
    const unsigned long nextNearMax = 0xFFFFFF00UL;
    TEST_ASSERT_FALSE(wlanMsBeforeDeadline(nowAfterWrap, nextNearMax));
    TEST_ASSERT_TRUE(nowAfterWrap < nextNearMax);

    // Cooldown that itself wraps: still pending until nextAllowed.
    const unsigned long nowNearMax = 0xFFFFFFF0UL;
    const unsigned long nextAfterWrap = 0x00000020UL;
    TEST_ASSERT_TRUE(wlanMsBeforeDeadline(nowNearMax, nextAfterWrap));
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

void test_soft_off_blocked_when_factory_owns() {
    TEST_ASSERT_FALSE(softOffAllowed(false, true, false));
    TEST_ASSERT_FALSE(softOffAllowed(false, false, true));
    TEST_ASSERT_FALSE(softOffAllowed(false, true, true));
    TEST_ASSERT_TRUE(softOffAllowed(false, false, false));
    TEST_ASSERT_FALSE(softOffAllowed(false, false, false, true, false, false));
    TEST_ASSERT_FALSE(softOffAllowed(false, false, false, false, true, false));
    TEST_ASSERT_FALSE(softOffAllowed(false, false, false, false, false, true));
}

void test_recovery_should_note_restart_only_after_claim() {
    TEST_ASSERT_TRUE(recoveryShouldNoteRestart(true));
    TEST_ASSERT_FALSE(recoveryShouldNoteRestart(false));

    const RecoveryRestartNote unsynced = recoveryNextRestartNote(0U, 100U, 2U);
    TEST_ASSERT_EQUAL_UINT32(0U, unsynced.day);
    TEST_ASSERT_EQUAL_UINT8(0U, unsynced.n);

    const RecoveryRestartNote first = recoveryNextRestartNote(200U, 0U, 0U);
    TEST_ASSERT_EQUAL_UINT32(200U, first.day);
    TEST_ASSERT_EQUAL_UINT8(1U, first.n);

    const RecoveryRestartNote next = recoveryNextRestartNote(200U, 200U, 2U);
    TEST_ASSERT_EQUAL_UINT32(200U, next.day);
    TEST_ASSERT_EQUAL_UINT8(3U, next.n);

    const RecoveryRestartNote rollover = recoveryNextRestartNote(201U, 200U, 3U);
    TEST_ASSERT_EQUAL_UINT32(201U, rollover.day);
    TEST_ASSERT_EQUAL_UINT8(1U, rollover.n);

    const RecoveryRestartNote cap = recoveryNextRestartNote(200U, 200U, 255U);
    TEST_ASSERT_EQUAL_UINT32(200U, cap.day);
    TEST_ASSERT_EQUAL_UINT8(255U, cap.n);
}

void test_wifi_sta_password_apply() {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WifiStaPasswordApply::KeepStored),
                          static_cast<int>(wifiStaPasswordApply(false, true)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WifiStaPasswordApply::Reject),
                          static_cast<int>(wifiStaPasswordApply(false, false)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WifiStaPasswordApply::UseProvided),
                          static_cast<int>(wifiStaPasswordApply(true, true)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WifiStaPasswordApply::UseProvided),
                          static_cast<int>(wifiStaPasswordApply(true, false)));
}

void test_wlan_mdns_kick_should_consume() {
    TEST_ASSERT_TRUE(wlanMdnsKickShouldConsume(false, false, true));
    TEST_ASSERT_FALSE(wlanMdnsKickShouldConsume(true, false, true));
    TEST_ASSERT_FALSE(wlanMdnsKickShouldConsume(false, true, true));
    TEST_ASSERT_FALSE(wlanMdnsKickShouldConsume(false, false, false));
}

void test_factory_wipe_should_abort() {
    TEST_ASSERT_FALSE(factoryWipeShouldAbort(true, false, true));
    TEST_ASSERT_FALSE(factoryWipeShouldAbort(true, true, false));
    TEST_ASSERT_FALSE(factoryWipeShouldAbort(false, false, true));
    TEST_ASSERT_TRUE(factoryWipeShouldAbort(false, false, false));
    TEST_ASSERT_FALSE(factoryWipeShouldAbort(false, true, false));
    TEST_ASSERT_FALSE(factoryWipeMustRestartUnready(true, false, false));
    TEST_ASSERT_TRUE(factoryWipeMustRestartUnready(false, true, false));
    TEST_ASSERT_TRUE(factoryWipeMustRestartUnready(false, false, true));
    TEST_ASSERT_FALSE(factoryWipeMustRestartUnready(false, false, false));
}

void test_nvs_write_allowed_after_lock() {
    TEST_ASSERT_TRUE(nvsWriteAllowedAfterLock(false, false, kNvsNsChaya));
    TEST_ASSERT_TRUE(nvsWriteAllowedAfterLock(true, false, kNvsNsChaya));
    TEST_ASSERT_FALSE(nvsWriteAllowedAfterLock(false, true, kNvsNsChaya));
    TEST_ASSERT_FALSE(nvsWriteAllowedAfterLock(true, true, kNvsNsChaya));
    TEST_ASSERT_TRUE(nvsWriteAllowedAfterLock(false, true, kNvsNsWifi));
    TEST_ASSERT_FALSE(nvsWriteAllowedAfterLock(true, false, kNvsNsWifi));
    TEST_ASSERT_FALSE(nvsWriteAllowedAfterLock(true, false, nullptr));
}

void test_wlan_force_caller_should_undo() {
    TEST_ASSERT_TRUE(wlanForceCallerShouldUndo(WlanForceReassocResult::Deferred));
    TEST_ASSERT_FALSE(wlanForceCallerShouldUndo(WlanForceReassocResult::Begun));
    TEST_ASSERT_FALSE(wlanForceCallerShouldUndo(WlanForceReassocResult::SkippedConnected));
    TEST_ASSERT_TRUE(wlanForceCallerShouldCountFail(WlanForceReassocResult::Begun));
    TEST_ASSERT_FALSE(wlanForceCallerShouldCountFail(WlanForceReassocResult::SkippedConnected));
    TEST_ASSERT_FALSE(wlanForceCallerShouldCountFail(WlanForceReassocResult::Deferred));
}

void test_wifi_scan_refresh_always_sets_kick() {
    TEST_ASSERT_TRUE(wifiScanRefreshSetsKick(false));
    TEST_ASSERT_TRUE(wifiScanRefreshSetsKick(true));
    TEST_ASSERT_TRUE(wifiScanServiceMayStartKick(false));
    TEST_ASSERT_FALSE(wifiScanServiceMayStartKick(true));
}

void test_wlan_nvs_invalid_cfg_v2_skips_legacy() {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(NvsBlobLoad::UseBlob),
                          static_cast<int>(nvsBlobLoadDecide(sizeof(PackedWifiConfigV2), sizeof(PackedWifiConfigV2))));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(NvsBlobLoad::UseLegacy),
                          static_cast<int>(nvsBlobLoadDecide(0, sizeof(PackedWifiConfigV2))));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(NvsBlobLoad::UseDefaults),
                          static_cast<int>(nvsBlobLoadDecide(4, sizeof(PackedWifiConfigV2))));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(NvsBlobLoad::UseDefaults),
                          static_cast<int>(nvsBlobLoadDecide(sizeof(PackedWifiConfigV2) + 8U, sizeof(PackedWifiConfigV2))));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_setup_ap_pass_syntax_and_format);
    RUN_TEST(test_setup_ap_pass_ensure_idempotent);
    RUN_TEST(test_wlan_boot_decision_keeps_configured_device_out_of_setup_ap);
    RUN_TEST(test_wlan_got_ip_decide_continue_sta_then_first_got_ip_runs_finish);
    RUN_TEST(test_wlan_got_ip_decide_second_got_ip_plays_wifi_up);
    RUN_TEST(test_wlan_got_ip_decide_boot_pending_finish_and_settled);
    RUN_TEST(test_wlan_got_ip_decide_finish_already_done_no_second_finish);
    RUN_TEST(test_wlan_got_ip_decide_status_finish_first_got_ip_no_second_wifi_up);
    RUN_TEST(test_wlan_wifi_test_owns_radio);
    RUN_TEST(test_wifi_qr_payload);
    RUN_TEST(test_wifi_ssid_syntax);
    RUN_TEST(test_ipv4_and_netmask_validation);
    RUN_TEST(test_wlan_config_validate);
    RUN_TEST(test_wlan_pack_roundtrip);
    RUN_TEST(test_wlan_recovery_decide);
    RUN_TEST(test_wifi_soft_reconnect_escalation_threshold);
    RUN_TEST(test_wlan_epd_tx_power_from_rssi);
    RUN_TEST(test_wlan_net_cmd_coalesce_enqueue);
    RUN_TEST(test_wifi_scan_defer_while_ota);
    RUN_TEST(test_wlan_scan_deadline_wrap);
    RUN_TEST(test_wlan_unpack_invalid_static_falls_back_dhcp);
    RUN_TEST(test_soft_off_blocked_when_factory_owns);
    RUN_TEST(test_recovery_should_note_restart_only_after_claim);
    RUN_TEST(test_wifi_sta_password_apply);
    RUN_TEST(test_wlan_mdns_kick_should_consume);
    RUN_TEST(test_factory_wipe_should_abort);
    RUN_TEST(test_nvs_write_allowed_after_lock);
    RUN_TEST(test_wlan_force_caller_should_undo);
    RUN_TEST(test_wifi_scan_refresh_always_sets_kick);
    RUN_TEST(test_wlan_nvs_invalid_cfg_v2_skips_legacy);
    return UNITY_END();
}
