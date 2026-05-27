#include <Arduino.h>

#include "auth.h"

#include "auth_config.h"
#include "auth_internal.h"

#include "config/app_config.h"
#include "hw/button.h"
#include "display/display.h"
#include "mqtt/config.h"
#include "wifi/wlan.h"

#include "util/time_helpers.h"

#include <esp_log.h>
#include <esp_random.h>
#include <cstring>

#include "log_tag.h"

DEFINE_LOG_TAG("AUTH");

std::atomic<uint32_t>      s_challengeCode{0};
std::atomic<unsigned long> s_challengeStartedMs{0};
std::atomic<bool>          s_challengePending{false};

std::atomic<bool>          s_awaitingButtonConfirm{false};
std::atomic<unsigned long> s_confirmStartedMs{0};

void challengeClearAtomic() {
    s_challengePending.store(false, std::memory_order_release);
    s_challengeStartedMs.store(0, std::memory_order_relaxed);
    s_challengeCode.store(0, std::memory_order_relaxed);
}

void awaitingClearAtomic() {
    s_awaitingButtonConfirm.store(false, std::memory_order_release);
    s_confirmStartedMs.store(0, std::memory_order_relaxed);
}

void challengeBeginAtomic(uint32_t code, unsigned long startedMs) {
    s_challengeCode.store(code, std::memory_order_relaxed);
    s_challengeStartedMs.store(startedMs, std::memory_order_release);
    s_challengePending.store(true, std::memory_order_release);
}

bool tryConsumeAuthChallenge(uint32_t entered, unsigned long nowMs) {
    if (!s_challengePending.load(std::memory_order_acquire)) {
        return false;
    }
    if (deadlineReached(s_challengeStartedMs.load(std::memory_order_acquire), kChallengeTtlMs,
                        nowMs)) {
        return false;
    }
    if (entered != s_challengeCode.load(std::memory_order_relaxed)) {
        return false;
    }
    bool expectedPending = true;
    if (!s_challengePending.compare_exchange_strong(expectedPending, false,
                                                    std::memory_order_acq_rel)) {
        return false;
    }
    s_challengeCode.store(0, std::memory_order_relaxed);
    s_challengeStartedMs.store(0, std::memory_order_relaxed);
    return true;
}

void scheduleMainTaskScreenAfterAuthFlow() {
    MqttConfig cfg{};
    mqttCfgSnapshot(&cfg);
    if (cfg.server[0] != '\0' && !configIsApMode()) {
        requestDeferredDrawHeartScreen();
    } else {
        requestDeferredDrawSplashScreen();
    }
}

void authConfirmWindowExpired() {
    ESP_LOGI(TAG, "Auth button confirm window expired");
    awaitingClearAtomic();
    challengeClearAtomic();
    buttonRequestAuthBlinkOffFromAsync();
    scheduleMainTaskScreenAfterAuthFlow();
}

void authChallengeExpired() {
    ESP_LOGI(TAG, "Web auth challenge code expired");
    awaitingClearAtomic();
    challengeClearAtomic();
    buttonRequestAuthBlinkOffFromAsync();
    scheduleMainTaskScreenAfterAuthFlow();
}

void maybeStartAwaitingButtonConfirm(bool resetFailStreakFromGet) {
    if (s_awaitingButtonConfirm.load(std::memory_order_acquire)
        || s_challengePending.load(std::memory_order_acquire)) {
        return;
    }
    bool expectedAwaiting = false;
    if (!s_awaitingButtonConfirm.compare_exchange_strong(expectedAwaiting, true,
                                                         std::memory_order_acq_rel)) {
        return;
    }
    if (s_challengePending.load(std::memory_order_acquire)) {
        s_awaitingButtonConfirm.store(false, std::memory_order_release);
        return;
    }

    s_confirmStartedMs.store(millis(), std::memory_order_release);

    if (resetFailStreakFromGet) {
        s_authFailStreak.store(0, std::memory_order_relaxed);
    }
    if (!requestDeferredDrawAuthPromptChecked()) {
        s_awaitingButtonConfirm.store(false, std::memory_order_release);
        ESP_LOGW(TAG, "Auth prompt enqueue failed (display queue full)");
    }
}

void webAuthHandleButtonDuringAuthBlink() {
    if (!configGetWebAuthEnabled() || configIsApMode()) {
        return;
    }
    if (!s_awaitingButtonConfirm.load(std::memory_order_acquire)) {
        return;
    }

    const unsigned long nowMs = millis();
    if (deadlineReached(s_confirmStartedMs.load(std::memory_order_acquire), kConfirmWindowMs,
                        nowMs)) {
        return;
    }

    awaitingClearAtomic();

    const uint32_t code = (esp_random() % 999999U) + 1U;
    challengeBeginAtomic(code, nowMs);
    requestDeferredDrawAuthCode(code);
    buttonSetAuthBlinkActive(false);
}

void webAuthResetConfirmDeadline() {
    if (!s_awaitingButtonConfirm.load(std::memory_order_acquire)) {
        return;
    }
    s_confirmStartedMs.store(millis(), std::memory_order_release);
}

void webAuthLoop() {
    const unsigned long nowMs = millis();

    static uint32_t s_sessionEvictCounter = 0U;
    if (++s_sessionEvictCounter >= 120U) {
        s_sessionEvictCounter = 0U;
        portENTER_CRITICAL(&s_authMux);
        if (s_sessionActive
            && deadlineReached(s_sessionCreatedMs, kSessionCookieMaxAgeSec * 1000UL, nowMs)) {
            s_sessionActive    = false;
            s_sessionCreatedMs = 0;
            memset(s_sessionRaw, 0, sizeof(s_sessionRaw));
        }
        portEXIT_CRITICAL(&s_authMux);
    }

    if (s_awaitingButtonConfirm.load(std::memory_order_acquire) && buttonIsAuthBlinkActive()) {
        if (deadlineReached(s_confirmStartedMs.load(std::memory_order_acquire), kConfirmWindowMs,
                            nowMs)) {
            authConfirmWindowExpired();
            return;
        }
    }

    if (s_challengePending.load(std::memory_order_acquire)) {
        if (deadlineReached(s_challengeStartedMs.load(std::memory_order_acquire), kChallengeTtlMs,
                            nowMs)) {
            authChallengeExpired();
        }
    }
}
