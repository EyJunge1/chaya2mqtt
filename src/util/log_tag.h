#pragma once

/** ESP-IDF logging tag: real tag in debug builds, empty unused placeholder in release. */
#if defined(CORE_DEBUG_LEVEL) && CORE_DEBUG_LEVEL > 0
#define DEFINE_LOG_TAG(name) static const char* TAG __attribute__((unused)) = (name)
#else
#define DEFINE_LOG_TAG(name) static constexpr const char* TAG __attribute__((unused)) = ""
#endif
