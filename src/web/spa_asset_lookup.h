#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

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

inline auto spaPathEquals(const char *a, const char *b) -> bool {
    if (!a || !b) {
        return false;
    }
    return std::strcmp(a, b) == 0;
}

inline auto spaIsAssetPath(const char *uri) -> bool { return uri && std::strncmp(uri, "/assets/", 8) == 0; }

/**
 * Blob assets under /assets/ are stored gzip-compressed and served with
 * Content-Encoding: gzip. URLs keep normal extensions (.js/.css) — never .gz —
 * so Safari/iOS CNA can render them. index.html is not in the blob.
 */
inline auto spaAssetUsesGzip(const char *path) -> bool { return spaIsAssetPath(path); }

inline auto spaIsApiOrEventsPath(const char *uri) -> bool {
    if (!uri) {
        return false;
    }
    return std::strncmp(uri, "/api/", 5) == 0 || spaPathEquals(uri, "/events");
}

/** OS captive-portal connectivity checks (handled by dedicated routes in AP mode). */
inline auto spaIsCaptivePortalProbe(const char *uri) -> bool {
    if (!uri) {
        return false;
    }
    return spaPathEquals(uri, "/generate_204") || spaPathEquals(uri, "/gen_204") || spaPathEquals(uri, "/hotspot-detect.html") ||
           spaPathEquals(uri, "/library/test/success.html") || spaPathEquals(uri, "/canonical.html") ||
           spaPathEquals(uri, "/ncsi.txt") || spaPathEquals(uri, "/connecttest.txt") || spaPathEquals(uri, "/redirect") ||
           spaPathEquals(uri, "/success.txt") || spaPathEquals(uri, "/wpad.dat");
}

/** True when an unknown GET path should receive the SPA index (client router). */
inline auto spaShouldFallbackToIndex(const char *uri) -> bool {
    if (!uri || uri[0] != '/') {
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

inline auto spaFindAsset(const SpaAssetEntry *entries, size_t count, const char *uri) -> const SpaAssetEntry * {
    if (!entries || !uri) {
        return nullptr;
    }
    for (size_t i = 0; i < count; ++i) {
        if (spaPathEquals(entries[i].path, uri)) {
            return &entries[i];
        }
    }
    return nullptr;
}
