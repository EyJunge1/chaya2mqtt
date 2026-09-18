#pragma once

#include <cstddef>
#include <cstdint>

enum class NvsBlobLoad : uint8_t { UseBlob, UseDefaults };

inline auto nvsBlobLoadDecide(size_t blobLen, size_t expectedLen) -> NvsBlobLoad {
    if (blobLen == expectedLen) {
        return NvsBlobLoad::UseBlob;
    }
    return NvsBlobLoad::UseDefaults;
}
