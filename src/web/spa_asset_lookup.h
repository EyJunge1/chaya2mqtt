#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <string_view>

/** Lookup helpers for the embedded SPA blob (pure C++, native-testable). */

enum class SpaCacheClass : uint8_t {
    NoCache = 0,
    Immutable = 1,
};

struct SpaAssetEntry {
    const char *path; // request path, e.g. "/assets/index-abc.js"
    uint32_t offset;  // byte offset into gWebUiBlobStart
    uint32_t length;  // payload length (gzip-compressed bytes in the blob)
    const char *contentType;
    SpaCacheClass cache;
};

[[nodiscard]] inline auto spaPathEquals(std::string_view a, std::string_view b) -> bool { return a == b; }

[[nodiscard]] inline auto spaPathEquals(const char *a, const char *b) -> bool {
    if (a == nullptr || b == nullptr) {
        return false;
    }
    return spaPathEquals(std::string_view{a}, std::string_view{b});
}

[[nodiscard]] inline auto spaIsAssetPath(std::string_view uri) -> bool { return uri.starts_with("/assets/"); }

[[nodiscard]] inline auto spaIsAssetPath(const char *uri) -> bool {
    return uri != nullptr && spaIsAssetPath(std::string_view{uri});
}

/**
 * Blob assets under /assets/ are stored gzip-compressed and served with
 * Content-Encoding: gzip. URLs keep normal extensions (.js/.css) — never .gz —
 * so Safari/iOS CNA can render them. index.html is not in the blob.
 */
[[nodiscard]] inline auto spaAssetUsesGzip(std::string_view path) -> bool { return spaIsAssetPath(path); }

[[nodiscard]] inline auto spaAssetUsesGzip(const char *path) -> bool { return spaIsAssetPath(path); }

[[nodiscard]] inline auto spaIsApiOrEventsPath(std::string_view uri) -> bool {
    return uri.starts_with("/api/") || spaPathEquals(uri, "/events");
}

[[nodiscard]] inline auto spaIsApiOrEventsPath(const char *uri) -> bool {
    return uri != nullptr && spaIsApiOrEventsPath(std::string_view{uri});
}

/** OS captive-portal connectivity checks (handled by dedicated routes in AP mode). */
[[nodiscard]] inline auto spaIsCaptivePortalProbe(std::string_view uri) -> bool {
    return spaPathEquals(uri, "/generate_204") || spaPathEquals(uri, "/gen_204") || spaPathEquals(uri, "/hotspot-detect.html") ||
           spaPathEquals(uri, "/library/test/success.html") || spaPathEquals(uri, "/canonical.html") ||
           spaPathEquals(uri, "/ncsi.txt") || spaPathEquals(uri, "/connecttest.txt") || spaPathEquals(uri, "/redirect") ||
           spaPathEquals(uri, "/success.txt") || spaPathEquals(uri, "/wpad.dat");
}

[[nodiscard]] inline auto spaIsCaptivePortalProbe(const char *uri) -> bool {
    return uri != nullptr && spaIsCaptivePortalProbe(std::string_view{uri});
}

/** True when an unknown GET path should receive the SPA index (client router). */
[[nodiscard]] inline auto spaShouldFallbackToIndex(std::string_view uri) -> bool {
    if (uri.empty() || uri.front() != '/') {
        return false;
    }
    if (spaIsApiOrEventsPath(uri)) {
        return false;
    }
    if (spaIsAssetPath(uri)) {
        return false;
    }
    return true;
}

[[nodiscard]] inline auto spaShouldFallbackToIndex(const char *uri) -> bool {
    return uri != nullptr && spaShouldFallbackToIndex(std::string_view{uri});
}

[[nodiscard]] inline auto spaFindAsset(std::span<const SpaAssetEntry> entries, std::string_view uri) -> const SpaAssetEntry * {
    const auto it = std::ranges::find_if(entries, [uri](const SpaAssetEntry &e) -> bool {
        return e.path != nullptr && spaPathEquals(std::string_view{e.path}, uri);
    });
    return it == entries.end() ? nullptr : &*it;
}

[[nodiscard]] inline auto spaFindAsset(const SpaAssetEntry *entries, size_t count, const char *uri) -> const SpaAssetEntry * {
    if (entries == nullptr || uri == nullptr) {
        return nullptr;
    }
    return spaFindAsset(std::span{entries, count}, std::string_view{uri});
}
