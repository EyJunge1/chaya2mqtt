#include "tls_bundle_setup.h"

#include "tls_bundle.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <esp_crt_bundle.h>

static SemaphoreHandle_t s_caBundleMutex = nullptr;
static bool              s_caBundleInstalled = false;

void chayaTlsInfraInit() {
    if (s_caBundleMutex != nullptr) {
        return;
    }
    s_caBundleMutex = xSemaphoreCreateMutex();
    if (s_caBundleMutex == nullptr) {
        abort();
    }
}

bool chayaTlsEnsureCaBundleInstalled() {
    if (s_caBundleMutex == nullptr) {
        chayaTlsInfraInit();
    }
    if (xSemaphoreTake(s_caBundleMutex, portMAX_DELAY) != pdTRUE) {
        return false;
    }
    if (s_caBundleInstalled) {
        xSemaphoreGive(s_caBundleMutex);
        return true;
    }
    const size_t bundleLen =
        static_cast<size_t>(x509_crt_bundle_end - x509_crt_bundle_start);
    const bool ok = esp_crt_bundle_set(x509_crt_bundle_start, bundleLen) == ESP_OK;
    if (ok) {
        s_caBundleInstalled = true;
    }
    xSemaphoreGive(s_caBundleMutex);
    return ok;
}
