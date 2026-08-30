#include <unity.h>

#include "constants.h"
#include "web/csrf_pure.h"
#include "web/hex_codec.h"
#include "web/host_validate.h"
#include "web/spa_asset_lookup.h"
#include "web/sse_dirty_pure.h"

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

void test_form_bool_syntax() {
    TEST_ASSERT_TRUE(formBoolSyntaxOk("0"));
    TEST_ASSERT_TRUE(formBoolSyntaxOk("1"));
    TEST_ASSERT_TRUE(formBoolSyntaxOk("true"));
    TEST_ASSERT_TRUE(formBoolSyntaxOk("false"));
    TEST_ASSERT_FALSE(formBoolSyntaxOk("yes"));
    TEST_ASSERT_FALSE(formBoolSyntaxOk(""));
    TEST_ASSERT_FALSE(formBoolSyntaxOk(nullptr));
    TEST_ASSERT_TRUE(formBoolFromForm("1"));
    TEST_ASSERT_TRUE(formBoolFromForm("true"));
    TEST_ASSERT_FALSE(formBoolFromForm("0"));
    TEST_ASSERT_FALSE(formBoolFromForm("false"));
    TEST_ASSERT_TRUE(audioVolumeInRange(0));
    TEST_ASSERT_TRUE(audioVolumeInRange(100));
    TEST_ASSERT_FALSE(audioVolumeInRange(101));
    TEST_ASSERT_TRUE(quietHourInRange(23));
    TEST_ASSERT_FALSE(quietHourInRange(24));
}

void test_hex_codec_roundtrip() {
    const uint8_t raw[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                             0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0xff};
    char hex[33]{};
    hexEncode16(raw, hex);
    TEST_ASSERT_EQUAL_STRING("000102030405060708090a0b0c0d0eff", hex);

    uint8_t out[16]{};
    TEST_ASSERT_TRUE(hexDecode32Strict(hex, out));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(raw, out, 16);
    TEST_ASSERT_TRUE(secretsEqual16(raw, out));

    TEST_ASSERT_FALSE(hexDecode32Strict("00", out));
    TEST_ASSERT_FALSE(hexDecode32Strict(nullptr, out));
}

void test_csrf_submitted_matches_expected() {
    const uint8_t raw[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                             0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0xff};
    char hex[33]{};
    hexEncode16(raw, hex);
    TEST_ASSERT_TRUE(csrfSubmittedMatchesExpected(hex, raw));
    TEST_ASSERT_FALSE(csrfSubmittedMatchesExpected("00", raw));
    TEST_ASSERT_FALSE(csrfSubmittedMatchesExpected(nullptr, raw));
    TEST_ASSERT_FALSE(csrfSubmittedMatchesExpected(hex, nullptr));
    hex[0] = 'f';
    TEST_ASSERT_FALSE(csrfSubmittedMatchesExpected(hex, raw));
}

void test_csrf_rotation_timing() {
    TEST_ASSERT_FALSE(csrfTokenNeedsRotation(1000U, 1000U));
    TEST_ASSERT_FALSE(csrfTokenNeedsRotation(1000U + kCsrfTokenTtlMs - 1U, 1000U));
    TEST_ASSERT_TRUE(csrfTokenNeedsRotation(1000U + kCsrfTokenTtlMs, 1000U));
    TEST_ASSERT_TRUE(csrfPreviousTokenAllowed(5000U, 5000U));
    TEST_ASSERT_TRUE(csrfPreviousTokenAllowed(5000U + kCsrfTokenGraceMs - 1U, 5000U));
    TEST_ASSERT_FALSE(csrfPreviousTokenAllowed(5000U + kCsrfTokenGraceMs, 5000U));
    TEST_ASSERT_TRUE(csrfTokenNeedsRotation(100U, 200U));
}

void test_csrf_accept_or_policy() {
    TEST_ASSERT_TRUE(csrfAcceptSubmitted(true, false, false));
    TEST_ASSERT_TRUE(csrfAcceptSubmitted(false, true, true));
    TEST_ASSERT_FALSE(csrfAcceptSubmitted(false, true, false));
    TEST_ASSERT_FALSE(csrfAcceptSubmitted(false, false, true));
}

void test_host_validate() {
    constexpr const char* hostname = "chaya2mqtt-a1b2c3";
    TEST_ASSERT_FALSE(webHostCStringAllowed("", false, hostname, nullptr));
    TEST_ASSERT_TRUE(webHostCStringAllowed("", true, kDeviceHostname, nullptr));
    TEST_ASSERT_TRUE(webHostCStringAllowed("chaya2mqtt-a1b2c3", false, hostname, nullptr));
    TEST_ASSERT_TRUE(webHostCStringAllowed("Chaya2MQTT-a1b2c3.local", false, hostname, nullptr));
    TEST_ASSERT_TRUE(
        webHostCStringAllowed("chaya2mqtt-a1b2c3.local:80", false, hostname, nullptr));
    TEST_ASSERT_TRUE(webHostCStringAllowed("4.3.2.1", true, kDeviceHostname, nullptr));
    TEST_ASSERT_TRUE(webHostCStringAllowed("chaya2mqtt", true, kDeviceHostname, nullptr));
    TEST_ASSERT_TRUE(webHostCStringAllowed("chaya2mqtt.local", true, kDeviceHostname, nullptr));
    TEST_ASSERT_FALSE(webHostCStringAllowed("captive.apple.com", true, kDeviceHostname, nullptr));
    TEST_ASSERT_FALSE(webHostCStringAllowed("evil.example", true, kDeviceHostname, nullptr));
    TEST_ASSERT_FALSE(webHostCStringAllowed("chaya2mqtt.local", false, hostname, nullptr));
    TEST_ASSERT_FALSE(webHostCStringAllowed("evil.example", false, hostname, nullptr));
    TEST_ASSERT_TRUE(
        webHostCStringAllowed("192.168.1.2", false, hostname, "192.168.1.2"));
    TEST_ASSERT_TRUE(
        webHostCStringAllowed("192.168.1.2:8080", false, hostname, "192.168.1.2"));
    TEST_ASSERT_FALSE(
        webHostCStringAllowed("192.168.1.3", false, hostname, "192.168.1.2"));
    TEST_ASSERT_FALSE(webHostCStringAllowed("chaya2mqtt-a1b2c3", false, nullptr, nullptr));
}

void test_spa_asset_lookup() {
    static const SpaAssetEntry entries[] = {
        {"/", 0u, 10u, "text/html; charset=utf-8", SpaCacheClass::NoCache},
        {"/index.html", 10u, 20u, "text/html; charset=utf-8", SpaCacheClass::NoCache},
        {"/assets/app-abc.js", 30u, 40u, "application/javascript; charset=utf-8",
         SpaCacheClass::Immutable},
    };
    const size_t count = sizeof(entries) / sizeof(entries[0]);

    TEST_ASSERT_TRUE(spaIsAssetPath("/assets/app-abc.js"));
    TEST_ASSERT_FALSE(spaIsAssetPath("/update"));
    TEST_ASSERT_TRUE(spaIsApiOrEventsPath("/api/device"));
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

    const SpaAssetEntry* js = spaFindAsset(entries, count, "/assets/app-abc.js");
    TEST_ASSERT_NOT_NULL(js);
    TEST_ASSERT_EQUAL_UINT32(30u, js->offset);
    TEST_ASSERT_EQUAL_UINT32(40u, js->length);

    const SpaAssetEntry* index = spaFindIndex(entries, count);
    TEST_ASSERT_NOT_NULL(index);
    TEST_ASSERT_EQUAL_STRING("/index.html", index->path);

    TEST_ASSERT_EQUAL_INT(static_cast<int>(SpaCacheClass::Immutable),
                          static_cast<int>(spaCacheClassForPath("/assets/x.css")));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SpaCacheClass::NoCache),
                          static_cast<int>(spaCacheClassForPath("/index.html")));
    TEST_ASSERT_EQUAL_STRING("text/css; charset=utf-8", spaContentTypeForPath("/assets/a.css"));
    TEST_ASSERT_EQUAL_STRING("application/javascript; charset=utf-8",
                             spaContentTypeForPath("/assets/a.js"));
    TEST_ASSERT_FALSE(spaAssetUsesGzip("/index.html"));
    TEST_ASSERT_TRUE(spaAssetUsesGzip("/assets/a.js"));
}

void test_sse_tick_select_bits() {
    bool keepalive = false;
    TEST_ASSERT_EQUAL_UINT32(kSseChaya,
                             sseTickSelectBits(kSseChaya, 1000U, 0U, 8000U, &keepalive));
    TEST_ASSERT_FALSE(keepalive);

    keepalive = true;
    TEST_ASSERT_EQUAL_UINT32(0U, sseTickSelectBits(0U, 1000U, 500U, 8000U, &keepalive));
    TEST_ASSERT_FALSE(keepalive);

    keepalive = false;
    TEST_ASSERT_EQUAL_UINT32(kSseWifi | kSseDevice,
                             sseTickSelectBits(0U, 9000U, 500U, 8000U, &keepalive));
    TEST_ASSERT_TRUE(keepalive);

    keepalive = false;
    TEST_ASSERT_EQUAL_UINT32(kSseWifi | kSseDevice,
                             sseTickSelectBits(0U, 100U, 0U, 8000U, &keepalive));
    TEST_ASSERT_TRUE(keepalive);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_ui_pref_syntax);
    RUN_TEST(test_form_bool_syntax);
    RUN_TEST(test_hex_codec_roundtrip);
    RUN_TEST(test_csrf_submitted_matches_expected);
    RUN_TEST(test_csrf_rotation_timing);
    RUN_TEST(test_csrf_accept_or_policy);
    RUN_TEST(test_host_validate);
    RUN_TEST(test_spa_asset_lookup);
    RUN_TEST(test_sse_tick_select_bits);
    return UNITY_END();
}
