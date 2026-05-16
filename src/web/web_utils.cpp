#include "web_utils.h"

#include <ESPAsyncWebServer.h>
#include <cstdio>

AsyncResponseStream* beginResponseStreamOr500(AsyncWebServerRequest* req, const char* mime) {
    AsyncResponseStream* resp = req->beginResponseStream(mime);
    if (resp == nullptr) {
        req->send(500);
    }
    return resp;
}

void appendHtmlEscaped(Print& out, const char* s) {
    if (s == nullptr) {
        return;
    }
    for (; *s != '\0'; ++s) {
        switch (*s) {
            case '&':
                out.print(F("&amp;"));
                break;
            case '"':
                out.print(F("&quot;"));
                break;
            case '\'':
                out.print(F("&#39;"));
                break;
            case '<':
                out.print(F("&lt;"));
                break;
            case '>':
                out.print(F("&gt;"));
                break;
            default:
                out.print(*s);
                break;
        }
    }
}

void appendJsonEscapedCStr(Print& out, const char* str) {
    out.print('"');
    if (str == nullptr) {
        out.print('"');
        return;
    }
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(str); *p != '\0'; ++p) {
        const unsigned char c = *p;
        switch (c) {
            case '"':
                out.print(F("\\\""));
                break;
            case '\\':
                out.print(F("\\\\"));
                break;
            case '\b':
                out.print(F("\\b"));
                break;
            case '\f':
                out.print(F("\\f"));
                break;
            case '\n':
                out.print(F("\\n"));
                break;
            case '\r':
                out.print(F("\\r"));
                break;
            case '\t':
                out.print(F("\\t"));
                break;
            default:
                if (c < 0x20U) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
                    out.print(buf);
                } else {
                    out.write(static_cast<char>(c));
                }
                break;
        }
    }
    out.print('"');
}
