#include "task_handles.h"
#include "async/task_config.h"
#include "event_types.h"
#include "tls/tls_bundle_setup.h"

#include <cstdlib>

#include <esp_log.h>

#include "util/log_tag.h"

DEFINE_LOG_TAG("ASYNC");

QueueHandle_t      g_netCmdQueue     = nullptr;
QueueHandle_t      g_displayCmdQueue = nullptr;
QueueHandle_t      g_audioCmdQueue   = nullptr;
SemaphoreHandle_t  g_mqttClientMutex      = nullptr;
SemaphoreHandle_t  g_chayaPublishMutex    = nullptr;
SemaphoreHandle_t  g_heartDebounceMutex  = nullptr;
SemaphoreHandle_t  g_nvsMutex             = nullptr;
SemaphoreHandle_t  g_wifiTestMutex   = nullptr;
SemaphoreHandle_t  g_wifiApiMutex    = nullptr;

void asyncInfraInit() {
    // Queues + mutexes shared across tasks; abort if allocation fails.
    chayaTlsInfraInit();
    g_netCmdQueue     = xQueueCreate(kNetCmdQueueDepth, sizeof(NetCmd));
    g_displayCmdQueue = xQueueCreate(kDisplayCmdQueueDepth, sizeof(DisplayMsg));
    g_audioCmdQueue   = xQueueCreate(kAudioCmdQueueDepth, sizeof(AudioMsg));
    g_mqttClientMutex   = xSemaphoreCreateMutex();
    g_chayaPublishMutex   = xSemaphoreCreateMutex();
    g_heartDebounceMutex  = xSemaphoreCreateMutex();
    g_nvsMutex            = xSemaphoreCreateMutex();
    g_wifiTestMutex     = xSemaphoreCreateMutex();
    g_wifiApiMutex      = xSemaphoreCreateMutex();

    if (g_netCmdQueue == nullptr || g_displayCmdQueue == nullptr || g_audioCmdQueue == nullptr
        || g_mqttClientMutex == nullptr || g_chayaPublishMutex == nullptr
        || g_heartDebounceMutex == nullptr || g_nvsMutex == nullptr
        || g_wifiTestMutex == nullptr || g_wifiApiMutex == nullptr) {
        ESP_LOGE(TAG, "asyncInfraInit: queue/mutex allocation failed");
        abort();
    }
}

bool netCmdTrySend(NetCmd cmd, TickType_t waitTicks) {
    if (g_netCmdQueue == nullptr) {
        return false;
    }
    return xQueueSend(g_netCmdQueue, &cmd, waitTicks) == pdTRUE;
}
