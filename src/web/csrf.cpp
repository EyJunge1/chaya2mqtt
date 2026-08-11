#include "csrf.h"

#include "csrf_pure.h"
#include "hex_codec.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <cstring>
#include <esp_random.h>
#include <freertos/portmacro.h>

namespace {

portMUX_TYPE s_csrfMux = portMUX_INITIALIZER_UNLOCKED;
uint8_t      s_csrfTokenRaw[16]{};

bool csrfTokenMatches(const char* submitted) {
    portENTER_CRITICAL(&s_csrfMux);
    uint8_t expectedCopy[16];
    memcpy(expectedCopy, s_csrfTokenRaw, sizeof(expectedCopy));
    portEXIT_CRITICAL(&s_csrfMux);
    return csrfSubmittedMatchesExpected(submitted, expectedCopy);
}

} // namespace

void webCsrfInit() {
    portENTER_CRITICAL(&s_csrfMux);
    esp_fill_random(s_csrfTokenRaw, sizeof(s_csrfTokenRaw));
    portEXIT_CRITICAL(&s_csrfMux);
}

void webCsrfGetTokenHex(char* outHex33, size_t outLen) {
    if (outHex33 == nullptr || outLen < 33U) {
        return;
    }
    portENTER_CRITICAL(&s_csrfMux);
    hexEncode16(s_csrfTokenRaw, outHex33);
    portEXIT_CRITICAL(&s_csrfMux);
}

bool webCsrfValidatePost(AsyncWebServerRequest* req) {
    if (!req->hasParam("csrf_token", true)) {
        return false;
    }
    const AsyncWebParameter* p = req->getParam("csrf_token", true);
    if (p == nullptr) {
        return false;
    }
    return csrfTokenMatches(p->value().c_str());
}
