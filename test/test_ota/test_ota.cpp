#include <climits>
#include <unity.h>

#include "ota/github_parse.h"
#include "ota/ota_url_allow.h"
#include "ota/ota_health.h"
#include "ota/version_cmp.h"

void test_ota_health_window() {
    TEST_ASSERT_FALSE(otaHealthWindowElapsed(false, true, 1000UL, 40000UL));
    TEST_ASSERT_FALSE(otaHealthWindowElapsed(true, false, 1000UL, 40000UL));
    TEST_ASSERT_FALSE(otaHealthWindowElapsed(true, true, 0UL, 40000UL));
    TEST_ASSERT_FALSE(otaHealthWindowElapsed(true, true, 1000UL, 1000UL + kOtaHealthStableMs - 1UL));
    TEST_ASSERT_TRUE(otaHealthWindowElapsed(true, true, 1000UL, 1000UL + kOtaHealthStableMs));
    // AP fallback / settled path still validates after the same window.
    TEST_ASSERT_TRUE(otaHealthWindowElapsed(true, true, 50UL, 50UL + kOtaHealthStableMs));
    // Unsigned wraparound: settledAt near max, now wrapped past window.
    const unsigned long settledNearWrap = ULONG_MAX - 1000UL;
    TEST_ASSERT_TRUE(otaHealthWindowElapsed(true, true, settledNearWrap,
                                            settledNearWrap + kOtaHealthStableMs));
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
    TEST_ASSERT_TRUE(otaVersionIsNewer("v2062.1.0", "2061.12.999"));
    TEST_ASSERT_TRUE(otaVersionIsNewer("v2026.8.1", "dev"));
    TEST_ASSERT_FALSE(otaVersionIsNewer("v2026.8", "2026.7.1"));
    TEST_ASSERT_FALSE(otaVersionIsNewer("v2026.8.1-rc.1-extra", "2026.8.0"));
    TEST_ASSERT_TRUE(otaReleaseTagIsAllowed("v2026.8.1"));
    TEST_ASSERT_TRUE(otaReleaseTagIsAllowed("v2026.12.0-rc.1"));
    TEST_ASSERT_FALSE(otaReleaseTagIsAllowed("2026.8.1"));
    TEST_ASSERT_FALSE(otaReleaseTagIsAllowed("V2026.8.1"));
    TEST_ASSERT_FALSE(otaReleaseTagIsAllowed("v2026.0.1"));
    TEST_ASSERT_FALSE(otaReleaseTagIsAllowed("v2026.13.1"));
    TEST_ASSERT_FALSE(otaReleaseTagIsAllowed("v2026.08.1"));
    TEST_ASSERT_FALSE(otaReleaseTagIsAllowed("v2026.8.1-rc.0"));
}

void test_ota_github_release_select() {
    const char* json =
        "["
        "{\"tag_name\":\"v2026.8.2-rc.1\",\"draft\":false,\"prerelease\":true,"
        "\"assets\":[{\"name\":\"firmware.bin\"},{\"name\":\"firmware.sha256\"}]},"
        "{\"tag_name\":\"v2026.8.1\",\"draft\":false,\"prerelease\":false,"
        "\"assets\":[{\"name\":\"firmware.bin\"},{\"name\":\"firmware.sha256\"}]}"
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
    TEST_ASSERT_TRUE(otaJsonHasAssetName(json, "firmware.sha256"));
    TEST_ASSERT_FALSE(otaJsonHasAssetName(json, "missing.bin"));
    TEST_ASSERT_FALSE(otaJsonHasAssetName(
        "{\"name\":\"firmware.sha256\",\"assets\":[{\"name\":\"firmware.bin\"}]}",
        "firmware.sha256"));
    TEST_ASSERT_FALSE(otaJsonHasAssetName(
        "{\"assets\":[{\"name\":\"firmware.bin\","
        "\"uploader\":{\"name\":\"firmware.sha256\"}}]}",
        "firmware.sha256"));

    const char* withDraft =
        "["
        "{\"tag_name\":\"v2026.9.1-rc.1\",\"draft\":true,\"prerelease\":true},"
        "{\"tag_name\":\"v2026.9.1-rc.2\",\"draft\":false,\"prerelease\":true}"
        "]";
    TEST_ASSERT_TRUE(otaSelectReleaseFromListJson(withDraft, true, tag, sizeof(tag), &isPre));
    TEST_ASSERT_EQUAL_STRING("v2026.9.1-rc.2", tag);

    const char* unordered =
        "["
        "{\"body\":\"escaped tag_name v9999.1.1\","
        "\"prerelease\":true,\"draft\":false,\"tag_name\":\"v2026.7.1-rc.1\"},"
        "{\"draft\":false,\"tag_name\":\"v2026.9.1-rc.3\",\"prerelease\":true},"
        "{\"prerelease\":true,\"tag_name\":\"v2026.9.1-rc.2\",\"draft\":false}"
        "]";
    TEST_ASSERT_TRUE(otaSelectReleaseFromListJson(unordered, true, tag, sizeof(tag), &isPre));
    TEST_ASSERT_EQUAL_STRING("v2026.9.1-rc.3", tag);

    bool value = false;
    TEST_ASSERT_FALSE(otaParseJsonBoolField("{\"draft\":trueish}", "draft", &value));
    TEST_ASSERT_TRUE(
        otaParseJsonBoolField("{\"nested\":{\"draft\":true},\"draft\":false}", "draft", &value));
    TEST_ASSERT_FALSE(value);
    char escaped[32]{};
    TEST_ASSERT_TRUE(
        otaParseJsonStringField("{\"tag_name\":\"v2026.8.1\\\"x\"}", "tag_name", escaped,
                                sizeof(escaped)));
    TEST_ASSERT_EQUAL_STRING("v2026.8.1\"x", escaped);
}

void test_ota_download_url_allowlist() {
    TEST_ASSERT_TRUE(otaReleaseDownloadUrlAllowed(
        "https://github.com/EyJunge1/chaya2mqtt/releases/download/v2026.8.1/firmware.bin",
        OtaDownloadAsset::Firmware));
    TEST_ASSERT_TRUE(otaReleaseDownloadUrlAllowed(
        "https://github.com/EyJunge1/chaya2mqtt/releases/download/v2026.8.1-rc.1/"
        "firmware.sha256",
        OtaDownloadAsset::Sha256));
    TEST_ASSERT_FALSE(otaReleaseDownloadUrlAllowed(
        "http://github.com/EyJunge1/chaya2mqtt/releases/download/v2026.8.1/firmware.bin",
        OtaDownloadAsset::Firmware));
    TEST_ASSERT_FALSE(otaReleaseDownloadUrlAllowed(
        "https://evil.example/EyJunge1/chaya2mqtt/releases/download/v2026.8.1/firmware.bin",
        OtaDownloadAsset::Firmware));
    TEST_ASSERT_FALSE(otaReleaseDownloadUrlAllowed(
        "https://github.com/EyJunge1/chaya2mqtt/releases/download/v2026.08.1/firmware.bin",
        OtaDownloadAsset::Firmware));
    TEST_ASSERT_FALSE(otaReleaseDownloadUrlAllowed(
        "https://github.com/EyJunge1/chaya2mqtt/releases/download/v2026.8.1/"
        "firmware.factory.bin",
        OtaDownloadAsset::Firmware));
    TEST_ASSERT_FALSE(otaReleaseDownloadUrlAllowed(
        "https://github.com/EyJunge1/chaya2mqtt/releases/download/v2026.8.1/../firmware.bin",
        OtaDownloadAsset::Firmware));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_ota_version_compare);
    RUN_TEST(test_ota_github_release_select);
    RUN_TEST(test_ota_download_url_allowlist);
    RUN_TEST(test_ota_health_window);
    return UNITY_END();
}
