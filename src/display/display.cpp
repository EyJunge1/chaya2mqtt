#include "display.h"
#include "display_task_internal.h"
#include "internal.h"

#include "util/log_tag.h"
#include "wifi/wlan.h"

#include <Arduino.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>

DEFINE_LOG_TAG("DISP");

void displaySetDesiredHeartIcon(DisplayHeartIcon icon) {
    displayTaskSetDesiredHeartIcon(icon);
}

DisplayHeartIcon displayDesiredHeartIcon() {
    return displayTaskDesiredHeartIcon();
}

void requestHeartRedraw() {
    if (configIsApMode()) {
        return;
    }
    (void)displayPostHeartRedraw(pdMS_TO_TICKS(100));
}

bool requestHeartRedrawNonBlocking() {
    if (configIsApMode()) {
        return true;
    }
    return displayPostHeartRedraw(0);
}

void requestDeferredDrawSplashScreen() {
    displayTaskDrainDrawIdleSem();
    ESP_LOGI(TAG, "splash queued");
    if (displayPostMsg(DisplayMsg::Cmd::DrawSplash, kDrawOnlyIfViewChanged,
                       pdMS_TO_TICKS(100))) {
        displayTaskSetSplashDrawPending(false);
    } else {
        displayTaskSetSplashDrawPending(true);
    }
}

void requestDeferredDrawHeartScreen() {
    if (configIsApMode()) {
        return;
    }
    displayTaskDrainDrawIdleSem();
    displayPostMsg(DisplayMsg::Cmd::DrawHeart, kDrawOnlyIfViewChanged, pdMS_TO_TICKS(100));
}

bool displayWaitDrawIdle(uint32_t timeoutMs) {
    return displayTaskWaitDrawIdle(timeoutMs);
}

bool displayDrawPowerOffAndWait(uint32_t timeoutMs) {
    return displayTaskDrawPowerOffAndWait(timeoutMs);
}

void displayInit() {
    displayHwInitPins();
    /*
     * GxEPD2-style: serial_diag_bitrate, initial_full_refresh, reset_duration_ms, pulldown_rst_mode.
     * Do not register loopTask with esp_task_wdt: full-window 4C e-paper refresh can block ~20s
     * inside nextPage(), which would trigger a task WDT abort. Long draws are expected on this device.
     */
    displayInitGxEpd();
}

void displayStartTask() {
    displayTaskStart();
}
