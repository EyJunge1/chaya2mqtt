#pragma once

/** GitHub daily/manual check + pending firmware download (call from webAdminLoop). */
void otaLoop();

/** Queue GitHub latest-release version check (may trigger firmware download). */
void otaQueueGithubCheck();
