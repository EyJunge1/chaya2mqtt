#pragma once

#include <cstddef>
#include <cstring>

/** Six-digit decimal auth code grammar (host-testable). */
inline bool webAuthCodeSyntaxOk(const char* codeStr) {
    if (codeStr == nullptr) {
        return false;
    }
    if (strlen(codeStr) != 6U) {
        return false;
    }
    for (size_t i = 0; i < 6U; ++i) {
        if (codeStr[i] < '0' || codeStr[i] > '9') {
            return false;
        }
    }
    return true;
}
