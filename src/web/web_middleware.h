#pragma once

#include <ESPAsyncWebServer.h>

ArMiddlewareCallback mwRequireAllowedHost();

ArMiddlewareCallback mwApiApPost();
ArMiddlewareCallback mwApiStaMode();
ArMiddlewareCallback mwApiApMode();
