#pragma once

#include "ota.h"

struct OtaReleaseInfo {
    char tag[64]{};
    char version[64]{}; // tag without leading 'v'
    char binUrl[256]{};
    char sha256Url[256]{};
    OtaChannel channel = OtaChannel::Stable;
    bool       isPrerelease = false;
};

enum class GithubCheckResult {
    ApiError           = 0,
    ParsedNoUpgrade    = 1,
    ParsedUpgradeAvail = 2,
};

/** Resolve the newest release for `channel` and compare against APP_VERSION. */
GithubCheckResult otaGithubEvaluateChannel(OtaChannel channel, OtaReleaseInfo* out);
