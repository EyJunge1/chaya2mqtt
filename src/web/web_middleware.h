#pragma once

#include <ESPAsyncWebServer.h>

ArMiddlewareCallback mwRequireAllowedHost();

ArMiddlewareCallback mwRequireStaMode();
ArMiddlewareCallback mwRequireApMode();

ArMiddlewareCallback mwRequireSessionRedirectGet();

ArMiddlewareCallback mwPostSessionAndCsrfRedirect(const char* csrfRedirectPath);

ArMiddlewareCallback mwPostChayaSendGuard();

ArMiddlewareCallback mwWifiConnectPostGuard();

ArMiddlewareCallback mwApPostCsrfRedirect(const char* redirectOnMismatch);

ArMiddlewareCallback mwWifiInfoOrApOpenGet();
