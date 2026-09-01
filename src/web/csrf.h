#pragma once

#include <cstddef>
#include <cstdint>

class AsyncWebServerRequest;

/** Generate the device CSRF token (call once before registering routes). */
void webCsrfInit();

/** Copy current CSRF token as 32 hex chars + NUL into outHex33 (outLen >= 33). */
void webCsrfGetTokenHex(char *outHex33, size_t outLen, uint32_t *outExpiresInSeconds = nullptr);

/** Header carrying the CSRF token on JSON mutations (not query, not body). */
constexpr const char kCsrfHeaderName[] = "X-CSRF-Token";

/** @return true if POST has a valid X-CSRF-Token matching the device token. */
bool webCsrfValidatePost(AsyncWebServerRequest *req);
