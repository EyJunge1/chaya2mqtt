#pragma once

#include <cstddef>
#include <cstdint>

class AsyncWebServerRequest;

/** Generate the device CSRF token (call once before registering routes). */
void webCsrfInit();

/** Copy current CSRF token as 32 hex chars + NUL into outHex33 (outLen >= 33). */
void webCsrfGetTokenHex(char* outHex33, size_t outLen, uint32_t* outExpiresInSeconds = nullptr);

/** @return true if POST has valid csrf_token matching device token. */
bool webCsrfValidatePost(AsyncWebServerRequest* req);
