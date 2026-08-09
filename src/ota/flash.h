#pragma once

#include <cstddef>
#include <cstdint>

/** Download .bin via TLS + HTTPUpdate, verify with preloaded MD5 sidecar. */
bool otaFlashVerifiedInstall(const char* binUrl, const char* md5Url);
