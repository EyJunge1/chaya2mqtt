#pragma once

#include <ESPAsyncWebServer.h>

ArMiddlewareCallback mwRequireAllowedHost();

/** JSON APIs: 403 on bad host/origin/CSRF instead of redirects. */
ArMiddlewareCallback mwApiPostCsrf();
ArMiddlewareCallback mwApiApPostCsrf();
ArMiddlewareCallback mwApiStaMode();
ArMiddlewareCallback mwApiApMode();
