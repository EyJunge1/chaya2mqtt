#include "task_handles.h"
#include "event_types.h"

#include <cstdlib>

#include <esp_log.h>

QueueHandle_t      g_netCmdQueue     = nullptr;
QueueHandle_t      g_displayCmdQueue = nullptr;
SemaphoreHandle_t  g_mqttClientMutex      = nullptr;
SemaphoreHandle_t  g_chayaPublishMutex    = nullptr;
SemaphoreHandle_t  g_heartDebounceMutex  = nullptr;
SemaphoreHandle_t  g_nvsMutex             = nullptr;
SemaphoreHandle_t  g_wifiTestMutex   = nullptr;
SemaphoreHandle_t  g_wifiApiMutex    = nullptr;

void asyncInfraInit() {
    // Queues + mutexes shared across tasks; abort if allocation fails.
    g_netCmdQueue     = xQueueCreate(16, sizeof(NetCmd));
    g_displayCmdQueue = xQueueCreate(16, sizeof(DisplayMsg));
    g_mqttClientMutex   = xSemaphoreCreateMutex();
    g_chayaPublishMutex   = xSemaphoreCreateMutex();
    g_heartDebounceMutex  = xSemaphoreCreateMutex();
    g_nvsMutex            = xSemaphoreCreateMutex();
    g_wifiTestMutex     = xSemaphoreCreateMutex();
    g_wifiApiMutex      = xSemaphoreCreateMutex();

    if (g_netCmdQueue == nullptr || g_displayCmdQueue == nullptr
        || g_mqttClientMutex == nullptr || g_chayaPublishMutex == nullptr
        || g_heartDebounceMutex == nullptr || g_nvsMutex == nullptr
        || g_wifiTestMutex == nullptr || g_wifiApiMutex == nullptr) {
        ESP_LOGE("ASYNC", "asyncInfraInit: queue/mutex allocation failed");
        abort();
    }
}
