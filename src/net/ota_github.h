#pragma once

#include <cstddef>

/** Inspect GitHub `releases/latest` and semver-compare vs APP_VERSION (see ota_github.cpp). */
enum class GithubCheckResult {
    ApiError           = 0,
    ParsedNoUpgrade    = 1,
    ParsedUpgradeAvail = 2,
};

GithubCheckResult otaGithubEvaluateLatestRelease(char* firmwareUrlBuf, size_t firmwareUrlBufLen);
