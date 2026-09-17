#pragma once

#include <cstddef>
#include <cstdint>

enum class OtaChannel : uint8_t {
    Stable = 0,
    Beta = 1,
};

enum class OtaPhase : uint8_t {
    Idle = 0,
    Checking = 1,
    Available = 2,
    Downloading = 3,
    Verifying = 4,
    Rebooting = 5,
    Error = 6,
};

struct OtaStatus {
    OtaPhase phase = OtaPhase::Idle;
    OtaChannel channel = OtaChannel::Stable;
    char localVersion[32]{};
    char availableVersion[32]{};
    uint32_t bytesDone = 0;
    uint32_t bytesTotal = 0;
    char error[48]{};
    uint32_t generation = 0;
};

/** GitHub daily/manual check + pending firmware download (call from OTA task). */
void otaLoop();

/** Queue GitHub version check for the currently selected channel. */
void otaQueueGithubCheck();

/** Queue a GitHub check for `channel`. Persisted by the OTA task (not the HTTP caller). */
void otaQueueGithubCheck(OtaChannel channel);

/** Queue install of a previously discovered release (no-op if none available). */
void otaQueueInstall();

/** Persist update channel preference. */
auto otaSetChannel(OtaChannel channel) -> bool;

auto otaGetChannel() -> OtaChannel;

/** Load update channel from NVS before the HTTP server starts. */
void otaPreloadChannelFromNvs();

/** Drop channel / last-update RAM caches after factory NVS wipe (RC-LIFE-07). */
void otaResetRamAfterFactoryClear();

/** Thread-safe status snapshot for API / SSE. */
void otaCopyStatus(OtaStatus *out);

auto otaPhaseName(OtaPhase phase) -> const char *;
auto otaChannelName(OtaChannel channel) -> const char *;

/** Mark pending-verify OTA image valid after boot health checks (call once from app task). */
void otaTryMarkValidAfterHealthCheck();

/** True while verified OTA download/flash is in progress. */
auto otaFlashInProgress() -> bool;

/** True if factory reset / reboot should be deferred (OTA active). */
auto otaBlocksDestructiveAction() -> bool;

/** Progress hook used by flash adapter (OTA task). */
void otaNotifyFlashProgress(uint32_t done, uint32_t total);

void otaNotifyFlashVerifying();
