#include "web_utils.h"

#include "auth.h"

#include <ESPAsyncWebServer.h>
#include <cstdio>
#include <cstring>

AsyncResponseStream* beginResponseStreamOr500(AsyncWebServerRequest* req, const char* mime) {
    AsyncResponseStream* resp = req->beginResponseStream(mime);
    if (resp == nullptr) {
        req->send(500);
    }
    return resp;
}

void appendCurrentWebCsrfTokenEscaped(Print& out) {
    char b[24];
    snprintf(b, sizeof(b), "%lu", static_cast<unsigned long>(webAuthGetCsrfToken()));
    appendHtmlEscaped(out, b);
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

bool appendJsonStringQuotedEscaped(const char* str, char* buf, size_t bufLen, size_t* inOutPos) {
    if (buf == nullptr || inOutPos == nullptr || *inOutPos >= bufLen) {
        return false;
    }
    size_t pos = *inOutPos;
    auto appendRaw = [&](const char* s, size_t len) -> bool {
        if (pos + len >= bufLen) {
            return false;
        }
        memcpy(buf + pos, s, len);
        pos += len;
        return true;
    };
    auto appendByte = [&](unsigned char c) -> bool {
        if (pos + 1U >= bufLen) {
            return false;
        }
        buf[pos++] = static_cast<char>(c);
        return true;
    };

    if (!appendByte('"')) {
        return false;
    }
    if (str == nullptr) {
        if (!appendByte('"')) {
            return false;
        }
        *inOutPos = pos;
        return true;
    }
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(str); *p != '\0'; ++p) {
        const unsigned char c = *p;
        switch (c) {
            case '"':
                if (!appendRaw("\\\"", 2)) {
                    return false;
                }
                break;
            case '\\':
                if (!appendRaw("\\\\", 2)) {
                    return false;
                }
                break;
            case '\b':
                if (!appendRaw("\\b", 2)) {
                    return false;
                }
                break;
            case '\f':
                if (!appendRaw("\\f", 2)) {
                    return false;
                }
                break;
            case '\n':
                if (!appendRaw("\\n", 2)) {
                    return false;
                }
                break;
            case '\r':
                if (!appendRaw("\\r", 2)) {
                    return false;
                }
                break;
            case '\t':
                if (!appendRaw("\\t", 2)) {
                    return false;
                }
                break;
            default:
                if (c < 0x20U) {
                    char ubuf[8];
                    static_cast<void>(snprintf(ubuf, sizeof(ubuf), "\\u%04x", static_cast<unsigned>(c)));
                    if (!appendRaw(ubuf, 6)) {
                        return false;
                    }
                } else {
                    if (!appendByte(c)) {
                        return false;
                    }
                }
                break;
        }
    }
    if (!appendByte('"')) {
        return false;
    }
    *inOutPos = pos;
    return true;
}
