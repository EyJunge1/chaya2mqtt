#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

#include "constants.h"
#include "util/format_buf.h"

enum class DeviceIdFormatError : uint8_t { BadArgs, FormatFailed };

/** Format 3 bytes as 6 lowercase hex chars. */
[[nodiscard]] inline auto deviceIdFormatFromBytes(std::span<const uint8_t, 3> bytes, std::span<char> out)
    -> std::expected<void, DeviceIdFormatError> {
    if (out.size() < kDeviceIdBufLen) {
        if (!out.empty()) {
            out[0] = '\0';
        }
        return std::unexpected(DeviceIdFormatError::BadArgs);
    }
    if (!formatToBuf(out, "{:02x}{:02x}{:02x}", bytes[0], bytes[1], bytes[2]) || !deviceIdSyntaxOk(out.data())) {
        out[0] = '\0';
        return std::unexpected(DeviceIdFormatError::FormatFailed);
    }
    return {};
}

/** C-array / pointer overload for existing call sites. */
[[nodiscard]] inline auto deviceIdFormatFromBytes(const uint8_t bytes[3], char *out, size_t outLen) -> bool {
    if (bytes == nullptr || out == nullptr) {
        if (out != nullptr && outLen > 0U) {
            out[0] = '\0';
        }
        return false;
    }
    return deviceIdFormatFromBytes(std::span<const uint8_t, 3>{bytes, 3}, std::span<char>{out, outLen}).has_value();
}
