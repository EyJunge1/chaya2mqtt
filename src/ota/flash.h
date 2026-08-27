#pragma once

/** Download .bin via TLS + HTTPUpdate and verify against a SHA-256 sidecar. */
bool otaFlashVerifiedInstall(const char* binUrl, const char* sha256Url);
