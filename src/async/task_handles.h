#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

extern QueueHandle_t      g_netCmdQueue;
extern QueueHandle_t      g_displayCmdQueue;
extern SemaphoreHandle_t  g_mqttClientMutex;
/** Chaya publish path (button/web vs MQTT). */
extern SemaphoreHandle_t  g_chayaPublishMutex;
/** Heart counter NVS debounce (maybeSave/flush). */
extern SemaphoreHandle_t  g_heartDebounceMutex;
extern SemaphoreHandle_t  g_nvsMutex;
/** Wi-Fi connection test (web vs network task). */
extern SemaphoreHandle_t  g_wifiTestMutex;
/** WiFi API (Arduino/esp_wifi across handlers + network task). */
extern SemaphoreHandle_t  g_wifiApiMutex;

/** Allocate queues and mutexes; call once from setup. */
void asyncInfraInit();
