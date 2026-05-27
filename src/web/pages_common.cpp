#include "pages_internal.h"

#include "web/assets/styles.h"
#include "web/assets/common_js.h"
#include "web_utils.h"

#include <Arduino.h>

void printCommonCss(Print& out) {
    out.print(reinterpret_cast<const __FlashStringHelper*>(WEB_COMMON_CSS));
}

void streamPageHeader(Print& out, const char* title) {
    out.print(F("<!DOCTYPE html><html lang='en'><head><meta charset='utf-8'>"
                "<meta name='viewport' content='width=device-width,initial-scale=1'><title>"));
    appendHtmlEscaped(out, title);
    out.print(F("</title>"));
    printCommonCss(out);
    out.print(F("<script>"));
    out.print(reinterpret_cast<const __FlashStringHelper*>(COMMON_JS));
    out.print(F("</script></head><body><div id='toast' class='toast'></div>"));
}
