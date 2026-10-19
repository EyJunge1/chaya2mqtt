#pragma once

#include <cstddef>
#include <format>
#include <span>
#include <utility>

/** Format into a fixed C buffer; always NUL-terminates when out is non-empty. */
template <typename... Args>
[[nodiscard]] inline auto formatToBuf(std::span<char> out, std::format_string<Args...> fmt, Args &&...args) -> bool {
    if (out.empty()) {
        return false;
    }
    const auto result = std::format_to_n(out.data(), out.size() - 1U, fmt, std::forward<Args>(args)...);
    *result.out = '\0';
    return static_cast<size_t>(result.size) < out.size();
}
