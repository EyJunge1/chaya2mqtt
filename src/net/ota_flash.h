#pragma once

/** Download .bin via TLS, SHA256-verify, install to next OTA partition. */
bool otaFlashVerifiedInstall(const char* binUrl);
