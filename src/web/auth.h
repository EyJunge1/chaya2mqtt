#pragma once

#include <cstddef>
#include <cstdint>

class AsyncWebServer;
class AsyncWebServerRequest;

/** Register /auth and install CSRF token (call before webAdminRegisterRoutes or inside it). */
void webAuthInit();

/** Call once per main loop: expire stale auth challenges. */
void webAuthLoop();

/**
 * If web access protection is on and request has no session, send 302 to /auth and return true.
 * Public routes never redirect.
 */
bool webAuthRedirectIfUnauthenticated(AsyncWebServerRequest* req);

/** @return true if POST has valid csrf_token matching device token. */
bool webAuthValidateCsrfPost(AsyncWebServerRequest* req);

/** Session valid (or protection off / AP mode). */
bool webAuthIsAuthenticated(AsyncWebServerRequest* req);

void webAuthGetCsrfTokenHex(char* outHex33, size_t outLen);

/** Short button press while auth LED blink is active: confirm reveal or no-op during code phase. */
void webAuthHandleButtonDuringAuthBlink();

/**
 * Extend the confirm window after deferred E-Ink draw (drawAuthPrompt blocks main loop for seconds).
 * No-op unless still awaiting physical button confirmation.
 */
void webAuthResetConfirmDeadline();

/** Clear session cookie state (factory reset). */
void webAuthInvalidateSession();

void webAuthRegisterRoutes(AsyncWebServer& server);
