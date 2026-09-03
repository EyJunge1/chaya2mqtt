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

inline bool spaPathEquals(const char *a, const char *b) {
    if (!a || !b) {
        return false;
    }
    return std::strcmp(a, b) == 0;
}

inline bool spaIsAssetPath(const char *uri) { return uri && std::strncmp(uri, "/assets/", 8) == 0; }

/**
 * Blob assets under /assets/ are stored gzip-compressed and served with
 * Content-Encoding: gzip. URLs keep normal extensions (.js/.css) — never .gz —
 * so Safari/iOS CNA can render them. index.html is not in the blob.
 */
inline bool spaAssetUsesGzip(const char *path) { return spaIsAssetPath(path); }

inline bool spaIsApiOrEventsPath(const char *uri) {
    if (!uri) {
        return false;
    }
    return std::strncmp(uri, "/api/", 5) == 0 || spaPathEquals(uri, "/events");
}

/** OS captive-portal connectivity checks (handled by dedicated routes in AP mode). */
inline bool spaIsCaptivePortalProbe(const char *uri) {
    if (!uri) {
        return false;
    }
    return spaPathEquals(uri, "/generate_204") || spaPathEquals(uri, "/gen_204") || spaPathEquals(uri, "/hotspot-detect.html") ||
           spaPathEquals(uri, "/library/test/success.html") || spaPathEquals(uri, "/canonical.html") ||
           spaPathEquals(uri, "/ncsi.txt") || spaPathEquals(uri, "/connecttest.txt") || spaPathEquals(uri, "/redirect") ||
           spaPathEquals(uri, "/success.txt") || spaPathEquals(uri, "/wpad.dat");
}

/** True when an unknown GET path should receive the SPA index (client router). */
inline bool spaShouldFallbackToIndex(const char *uri) {
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

inline const SpaAssetEntry *spaFindAsset(const SpaAssetEntry *entries, size_t count, const char *uri) {
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

inline const SpaAssetEntry *spaFindIndex(const SpaAssetEntry *entries, size_t count) {
    const SpaAssetEntry *indexHtml = spaFindAsset(entries, count, "/index.html");
    if (indexHtml) {
        return indexHtml;
    }
    return spaFindAsset(entries, count, "/");
}

inline SpaCacheClass spaCacheClassForPath(const char *path) {
    if (!path) {
        return SpaCacheClass::NoCache;
    }
    // Hashed Vite assets live under /assets/ and are safe to cache forever.
    if (spaIsAssetPath(path)) {
        return SpaCacheClass::Immutable;
    }
    return SpaCacheClass::NoCache;
}

inline const char *spaContentTypeForPath(const char *path) {
    if (!path) {
        return "application/octet-stream";
    }
    const char *dot = std::strrchr(path, '.');
    if (!dot) {
        return "application/octet-stream";
    }
    if (std::strcmp(dot, ".html") == 0) {
        return "text/html; charset=utf-8";
    }
    if (std::strcmp(dot, ".js") == 0 || std::strcmp(dot, ".mjs") == 0) {
        return "application/javascript; charset=utf-8";
    }
    if (std::strcmp(dot, ".css") == 0) {
        return "text/css; charset=utf-8";
    }
    if (std::strcmp(dot, ".svg") == 0) {
        return "image/svg+xml";
    }
    if (std::strcmp(dot, ".png") == 0) {
        return "image/png";
    }
    if (std::strcmp(dot, ".jpg") == 0 || std::strcmp(dot, ".jpeg") == 0) {
        return "image/jpeg";
    }
    if (std::strcmp(dot, ".webp") == 0) {
        return "image/webp";
    }
    if (std::strcmp(dot, ".ico") == 0) {
        return "image/x-icon";
    }
    if (std::strcmp(dot, ".json") == 0) {
        return "application/json";
    }
    if (std::strcmp(dot, ".webmanifest") == 0) {
        return "application/manifest+json";
    }
    if (std::strcmp(dot, ".woff2") == 0) {
        return "font/woff2";
    }
    if (std::strcmp(dot, ".woff") == 0) {
        return "font/woff";
    }
    if (std::strcmp(dot, ".txt") == 0) {
        return "text/plain; charset=utf-8";
    }
    return "application/octet-stream";
}
