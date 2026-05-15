#pragma once

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

uint32_t webAuthGetCsrfToken();

/** Short button press during pending code: cancel challenge (from button, no include-cycle). */
void webAuthHandleButtonCancel();

/** Clear session cookie state (factory reset). */
void webAuthInvalidateSession();

void webAuthRegisterRoutes(AsyncWebServer& server);
