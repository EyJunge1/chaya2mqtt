#pragma once

/** Download .bin via TLS + HTTPUpdate and verify against a SHA-512 sidecar. */
auto otaFlashVerifiedInstall(const char *binUrl, const char *sha512Url) -> bool;
