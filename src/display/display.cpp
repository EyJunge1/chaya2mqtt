#include "display.h"
#include "display_task_internal.h"
#include "internal.h"

#include "mqtt/config.h"
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

bool displayRequest(DisplayMsg::Cmd cmd, DisplayRequestMode mode, uint32_t waitMs) {
    switch (mode) {
    case DisplayRequestMode::Content: {
        if (cmd != DisplayMsg::Cmd::DrawHeart) {
            ESP_LOGW(TAG, "displayRequest Content only supports DrawHeart");
            return false;
        }
        // SoftAP QR and waiting title (no broker yet): never overlay heart content.
        if (configIsApMode() || !mqttCfgIsBrokerConfigured()) {
            return true;
        }
        return displayPostHeartRedraw(pdMS_TO_TICKS(waitMs));
    }
    case DisplayRequestMode::BootIfChanged: {
        if (cmd == DisplayMsg::Cmd::DrawHeart) {
            if (configIsApMode()) {
                return true;
            }
            displayTaskDrainDrawIdleSem();
            return displayPostMsg(DisplayMsg::Cmd::DrawHeart, kDrawOnlyIfViewChanged,
                                 pdMS_TO_TICKS(waitMs));
        }
        if (cmd == DisplayMsg::Cmd::DrawSplash) {
            displayTaskDrainDrawIdleSem();
            ESP_LOGI(TAG, "splash queued");
            if (displayPostMsg(DisplayMsg::Cmd::DrawSplash, kDrawOnlyIfViewChanged,
                               pdMS_TO_TICKS(waitMs))) {
                displayTaskSetSplashDrawPending(false);
                return true;
            }
            displayTaskSetSplashDrawPending(true);
            return false;
        }
        ESP_LOGW(TAG, "displayRequest BootIfChanged unsupported cmd=%d", static_cast<int>(cmd));
        return false;
    }
    case DisplayRequestMode::PowerOffWait: {
        if (cmd != DisplayMsg::Cmd::DrawPowerOff) {
            ESP_LOGW(TAG, "displayRequest PowerOffWait only supports DrawPowerOff");
            return false;
        }
        return displayTaskDrawPowerOffAndWait(waitMs);
    }
    }
    return false;
}

bool displayWaitDrawIdle(uint32_t timeoutMs) {
    return displayTaskWaitDrawIdle(timeoutMs);
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
