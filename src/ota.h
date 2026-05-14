#pragma once

/** GitHub daily/manual check + pending firmware download (call from webAdminLoop). */
void otaLoop();

/** Queue OTA from custom HTTPS URL (copied into internal buffer). @return false if URL invalid/too long */
bool otaQueueFirmwareUrl(const char* url);

/** Queue GitHub latest-release version check (may trigger firmware download). */
void otaQueueGithubCheck();
