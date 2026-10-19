#pragma once

#include <ESPAsyncWebServer.h>

/** Host allowlist. Attach once on the server (`ws.addMiddleware`). */
ArMiddlewareCallback mwRequireAllowedHost();

/** STA-only API (`400 ap_mode`). Host is the server middleware. */
ArMiddlewareCallback mwApiStaMode();

/** AP-only API (`400 not_ap`). Host is the server middleware. */
ArMiddlewareCallback mwApiApMode();
