#pragma once

#include <cstddef>
#include <cstdint>

enum class NvsBlobLoad : uint8_t { UseBlob, UseLegacy, UseDefaults };

inline auto nvsBlobLoadDecide(size_t blobLen, size_t expectedLen) -> NvsBlobLoad {
    if (blobLen == expectedLen) {
        return NvsBlobLoad::UseBlob;
    }
    if (blobLen == 0U) {
        return NvsBlobLoad::UseLegacy; // missing key / migration
    }
    return NvsBlobLoad::UseDefaults; // present but wrong size
}
