#include "tls_bundle_setup.h"

#include "tls_bundle.h"

#include <atomic>
#include <esp_crt_bundle.h>

static std::atomic<bool> s_caBundleInstalled{false};

bool chayaTlsEnsureCaBundleInstalled() {
    if (s_caBundleInstalled.load(std::memory_order_acquire)) {
        return true;
    }
    const size_t bundleLen =
        static_cast<size_t>(x509_crt_bundle_end - x509_crt_bundle_start);
    if (esp_crt_bundle_set(x509_crt_bundle_start, bundleLen) != ESP_OK) {
        return false;
    }
    s_caBundleInstalled.store(true, std::memory_order_release);
    return true;
}
