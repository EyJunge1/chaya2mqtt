#pragma once

/** GitHub daily/manual check + pending firmware download (call from OTA task). */
void otaLoop();

/** Queue GitHub latest-release version check (may trigger firmware download). */
void otaQueueGithubCheck();

/** Mark pending-verify OTA image valid after boot health checks (call once from app task). */
void otaTryMarkValidAfterHealthCheck();

/** True while verified OTA download/flash is in progress. */
bool otaFlashInProgress();

/** True if factory reset / reboot should be deferred (OTA active). */
bool otaBlocksDestructiveAction();
