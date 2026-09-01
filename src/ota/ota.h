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

/** Persist channel and queue a GitHub version check. Returns false on NVS failure. */
bool otaQueueGithubCheck(OtaChannel channel);

/** Queue install of a previously discovered release (no-op if none available). */
void otaQueueInstall();

/** Persist update channel preference. */
bool otaSetChannel(OtaChannel channel);

OtaChannel otaGetChannel();

/** Thread-safe status snapshot for API / SSE. */
void otaCopyStatus(OtaStatus *out);

const char *otaPhaseName(OtaPhase phase);
const char *otaChannelName(OtaChannel channel);

/** Mark pending-verify OTA image valid after boot health checks (call once from app task). */
void otaTryMarkValidAfterHealthCheck();

/** True while verified OTA download/flash is in progress. */
bool otaFlashInProgress();

/** True if factory reset / reboot should be deferred (OTA active). */
bool otaBlocksDestructiveAction();

/** Progress hook used by flash adapter (OTA task). */
void otaNotifyFlashProgress(uint32_t done, uint32_t total);

void otaNotifyFlashVerifying();
