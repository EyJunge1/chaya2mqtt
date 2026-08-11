#include <unity.h>

#include "constants.h"
#include "web/csrf_pure.h"
#include "web/hex_codec.h"
#include "web/host_validate.h"
#include "web/spa_asset_lookup.h"

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

void test_display_dark_syntax() {
    TEST_ASSERT_TRUE(displayDarkSyntaxOk("0"));
    TEST_ASSERT_TRUE(displayDarkSyntaxOk("1"));
    TEST_ASSERT_TRUE(displayDarkSyntaxOk("true"));
    TEST_ASSERT_TRUE(displayDarkSyntaxOk("false"));
    TEST_ASSERT_FALSE(displayDarkSyntaxOk("yes"));
    TEST_ASSERT_FALSE(displayDarkSyntaxOk(""));
    TEST_ASSERT_FALSE(displayDarkSyntaxOk(nullptr));
    TEST_ASSERT_TRUE(displayDarkFromForm("1"));
    TEST_ASSERT_TRUE(displayDarkFromForm("true"));
    TEST_ASSERT_FALSE(displayDarkFromForm("0"));
    TEST_ASSERT_FALSE(displayDarkFromForm("false"));
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

void test_host_validate() {
    TEST_ASSERT_TRUE(webHostCStringAllowed("", false, nullptr));
    TEST_ASSERT_TRUE(webHostCStringAllowed("chaya2mqtt", false, nullptr));
    TEST_ASSERT_TRUE(webHostCStringAllowed("Chaya2MQTT.local", false, nullptr));
    TEST_ASSERT_TRUE(webHostCStringAllowed("chaya2mqtt.local:80", false, nullptr));
    TEST_ASSERT_TRUE(webHostCStringAllowed("captive.apple.com", true, nullptr));
    TEST_ASSERT_FALSE(webHostCStringAllowed("evil.example", false, nullptr));
    TEST_ASSERT_TRUE(webHostCStringAllowed("192.168.1.2", false, "192.168.1.2"));
    TEST_ASSERT_TRUE(webHostCStringAllowed("192.168.1.2:8080", false, "192.168.1.2"));
    TEST_ASSERT_FALSE(webHostCStringAllowed("192.168.1.3", false, "192.168.1.2"));
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
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_ui_pref_syntax);
    RUN_TEST(test_display_dark_syntax);
    RUN_TEST(test_hex_codec_roundtrip);
    RUN_TEST(test_csrf_submitted_matches_expected);
    RUN_TEST(test_host_validate);
    RUN_TEST(test_spa_asset_lookup);
    return UNITY_END();
}
