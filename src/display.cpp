#include "display.h"

#include "counter.h"
#include "pins.h"
#include "web/auth.h"

#include <GxEPD2_3C.h>
#include <Arduino.h>
#include <SPI.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <driver/gpio.h>
#include <esp_log.h>

#if defined(CORE_DEBUG_LEVEL) && CORE_DEBUG_LEVEL > 0
static const char* TAG = "DISP";
#else
static constexpr const char* TAG __attribute__((unused)) = "";
#endif

static GxEPD2_3C<GxEPD2_154_Z90c, GxEPD2_154_Z90c::HEIGHT> display(
    GxEPD2_154_Z90c(/*CS=*/ pins::kSpiCs, /*DC=*/ pins::kDisplayDc, /*RST=*/ pins::kDisplayRst,
                      /*BUSY=*/ pins::kDisplayBusy));

// BUSY/RST/DC: pinModes setzt main pinsInit() vor displayInit(); GxEPD2 nutzt diese vor init.

static std::atomic<bool> g_heartRedrawPending{false};
/** Scheduled from other FreeRTOS tasks for main-task-only E-Ink draws. */
static std::atomic<bool>     g_deferredAuthCodePending{false};
static std::atomic<uint32_t> g_deferredAuthCodeValue{0};
static std::atomic<bool>     g_deferredAuthPromptPending{false};
static std::atomic<bool>     g_deferredSplashPending{false};
static std::atomic<bool>     g_deferredHeartScreenPending{false};
static bool                  g_displaySpiSuspendedLowPower = false;

void requestHeartRedraw() {
    g_heartRedrawPending.store(true, std::memory_order_release);
}

bool consumeHeartRedraw() {
    return g_heartRedrawPending.exchange(false, std::memory_order_acq_rel);
}

void requestDeferredDrawAuthCode(uint32_t code) {
    g_deferredAuthCodeValue.store(code, std::memory_order_relaxed);
    g_deferredAuthCodePending.store(true, std::memory_order_release);
}

void requestDeferredDrawAuthPrompt() {
    g_deferredAuthPromptPending.store(true, std::memory_order_release);
}

void requestDeferredDrawSplashScreen() {
    g_deferredSplashPending.store(true, std::memory_order_release);
}

void requestDeferredDrawHeartScreen() {
    g_deferredHeartScreenPending.store(true, std::memory_order_release);
}

void displayProcessDeferredDrawsOnMainTask() {
    if (g_deferredAuthCodePending.exchange(false, std::memory_order_acq_rel)) {
        drawAuthCode(g_deferredAuthCodeValue.load(std::memory_order_relaxed));
        return;
    }
    if (g_deferredAuthPromptPending.exchange(false, std::memory_order_acq_rel)) {
        drawAuthPrompt();
        webAuthResetConfirmDeadline();
        return;
    }
    if (g_deferredSplashPending.exchange(false, std::memory_order_acq_rel)) {
        drawSplashScreen();
        return;
    }
    if (g_deferredHeartScreenPending.exchange(false, std::memory_order_acq_rel)) {
        drawHeartWithNumber();
    }
}

static void displayResumeSpiForDraw() {
    if (g_displaySpiSuspendedLowPower) {
        gpio_hold_dis(static_cast<gpio_num_t>(pins::kSpiCs));
        g_displaySpiSuspendedLowPower = false;
    }
    SPI.begin(/*SCK=*/ pins::kSpiSck, /*MISO=*/ pins::kSpiMiso, /*MOSI=*/ pins::kSpiMosi,
              /*SS=*/ pins::kSpiCs);
}

/** Nach Hibernate: SPI freigeben, Datenleitungen pullen; CS HIGH halten (Hold) fuer Light-Sleep. */
static void displaySuspendSpiLowPower() {
    SPI.end();
    pinMode(pins::kSpiSck, INPUT_PULLDOWN);
    pinMode(pins::kSpiMosi, INPUT_PULLDOWN);
    pinMode(pins::kSpiMiso, INPUT_PULLDOWN);
    pinMode(pins::kSpiCs, OUTPUT);
    digitalWrite(pins::kSpiCs, HIGH);
    gpio_hold_en(static_cast<gpio_num_t>(pins::kSpiCs));
    g_displaySpiSuspendedLowPower = true;
}

void displayInit() {
    SPI.begin(/*SCK=*/ pins::kSpiSck, /*MISO=*/ pins::kSpiMiso, /*MOSI=*/ pins::kSpiMosi,
              /*SS=*/ pins::kSpiCs);
    /*
     * Do not register loopTask with esp_task_wdt: full-window 3C e-paper refresh can block >5s inside
     * nextPage(), which would trigger a task WDT abort. Long draws are expected on this device.
     */
    /* Baud 0: avoid GxEPD2 UART spam ("Update_Full") on Serial; use ESP_LOG only in debug builds. */
    display.init(0, true, 2, false);
}

/** Small ↓ (incoming): tip points toward larger y. */
static void drawArrowDown(int16_t cx, int16_t tipY) {
    static constexpr int16_t kHalf = 12;
    static constexpr int16_t kStemH = 20;
    const int16_t baseY = static_cast<int16_t>(tipY - 5);
    display.fillTriangle(cx, tipY, static_cast<int16_t>(cx - kHalf), baseY,
                         static_cast<int16_t>(cx + kHalf), baseY, GxEPD_BLACK);
    display.fillRect(static_cast<int16_t>(cx - 2), static_cast<int16_t>(tipY - kStemH - 6),
                     4, kStemH, GxEPD_BLACK);
}

/** Small ↑ (outgoing): tip points toward smaller y. */
static void drawArrowUp(int16_t cx, int16_t tipY) {
    static constexpr int16_t kHalf = 12;
    static constexpr int16_t kStemH = 20;
    const int16_t baseY = static_cast<int16_t>(tipY + 5);
    display.fillTriangle(cx, tipY, static_cast<int16_t>(cx - kHalf), baseY,
                         static_cast<int16_t>(cx + kHalf), baseY, GxEPD_BLACK);
    display.fillRect(static_cast<int16_t>(cx - 2), static_cast<int16_t>(tipY + 6), 4, kStemH,
                     GxEPD_BLACK);
}

static uint8_t footerTextSizeForDigitCount(size_t digitLen) {
    return digitLen <= 3 ? 4 : 3;
}

/** Display delta vs baseline, cap at 999 with "999+" for overflow (layout-friendly). */
static void formatCappedCounterForDisplay(int rawCounter, int baseline, char* buf, size_t buflen) {
    const int64_t delta64 =
        static_cast<int64_t>(rawCounter) - static_cast<int64_t>(baseline);
    const int64_t shown64 = std::max<int64_t>(0, std::min<int64_t>(delta64, 9999));
    if (shown64 > 999) {
        static_cast<void>(snprintf(buf, buflen, "999+"));
    } else {
        static_cast<void>(snprintf(buf, buflen, "%lld", static_cast<long long>(shown64)));
    }
}

void drawHeartWithNumber() {
    displayResumeSpiForDraw();

    ESP_LOGI(TAG, "Zeichne rotes Herz mit Zaehlern...");

    static constexpr int kCenterX     = 100;
    static constexpr int kHeartSize  = 70;
    static constexpr int kCircleRadius = (kHeartSize / 2) + 8;
    static constexpr int kCircleSpacing = (kHeartSize / 2) - 3;
    /** Circle center Y: keeps round tops fully on-screen (200px height). */
    static constexpr int kCircleY      = 50;
    /** Triangle base is wider than the circle extent by 20 px (10 px each side). */
    static constexpr int kTriangleTop    = kCircleY + 15;
    static constexpr int kTriangleBottom  = 163;
    static constexpr int kMaxWidth       = 2 * (kCircleSpacing + kCircleRadius) - 4;

    const int dw = display.width();
    const int dh = display.height();

    const int16_t triLeftX   = static_cast<int16_t>(kCenterX - (kMaxWidth / 2));
    const int16_t triRightX  = static_cast<int16_t>(kCenterX + (kMaxWidth / 2));
    const int16_t triBottomY = static_cast<int16_t>(kTriangleBottom);

    char recvBuf[16];
    char sentBuf[16];
    formatCappedCounterForDisplay(heartCounter, counterBaseline, recvBuf, sizeof(recvBuf));
    formatCappedCounterForDisplay(heartSentCounter, sentCountBaseline, sentBuf, sizeof(sentBuf));
    const size_t recvLen = std::max<size_t>(strlen(recvBuf), size_t{1});
    const size_t sentLen = std::max<size_t>(strlen(sentBuf), size_t{1});

    const uint8_t recvTextSize = footerTextSizeForDigitCount(recvLen);
    const uint8_t sentTextSize = footerTextSizeForDigitCount(sentLen);

    /** Text top row: textSize 4 fits fully above y=200. */
    static constexpr int kFooterTextTop = 167;
    static constexpr int kLeftMargin       = 4;
    static constexpr int kRightMargin      = 4;
    static constexpr int kArrowLane        = 26;
    static constexpr int16_t kDownArrowCx    = 13;
    static constexpr int16_t kDownArrowTipY = 198;
    const int16_t           kUpArrowCx      = static_cast<int16_t>(dw - 13);
    static constexpr int16_t kUpArrowTipY = static_cast<int16_t>(kFooterTextTop + 1);

    int16_t rx1 = 0;
    int16_t ry1 = 0;
    uint16_t rw = 0;
    [[maybe_unused]] uint16_t rh = 0;
    int16_t sx1 = 0;
    int16_t sy1 = 0;
    uint16_t sw = 0;
    [[maybe_unused]] uint16_t sh = 0;

    display.setTextColor(GxEPD_BLACK);

    display.setTextSize(recvTextSize);
    display.getTextBounds(recvBuf, 0, 0, &rx1, &ry1, &rw, &rh);
    const int recvTextCursorX =
        kLeftMargin + kArrowLane - static_cast<int>(rx1);

    display.setTextSize(sentTextSize);
    display.getTextBounds(sentBuf, 0, 0, &sx1, &sy1, &sw, &sh);
    const int sentTextCursorX =
        dw - kRightMargin - kArrowLane - static_cast<int>(sw) - static_cast<int>(sx1);

    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);

        display.fillCircle(static_cast<int16_t>(kCenterX - kCircleSpacing),
                           static_cast<int16_t>(kCircleY),
                           static_cast<int16_t>(kCircleRadius), GxEPD_RED);
        display.fillCircle(static_cast<int16_t>(kCenterX + kCircleSpacing),
                           static_cast<int16_t>(kCircleY),
                           static_cast<int16_t>(kCircleRadius), GxEPD_RED);

        if (kTriangleTop >= 0 && kTriangleBottom < dh && triLeftX >= 0 && triRightX < dw) {
            display.fillTriangle(triLeftX, static_cast<int16_t>(kTriangleTop), triRightX,
                                 static_cast<int16_t>(kTriangleTop),
                                 static_cast<int16_t>(kCenterX), triBottomY, GxEPD_RED);
        }

        display.fillRect(static_cast<int16_t>(kCenterX - (kHeartSize / 3)),
                         static_cast<int16_t>(kCircleY - (kHeartSize / 6)),
                         static_cast<int16_t>((kHeartSize * 2) / 3),
                         static_cast<int16_t>(kHeartSize / 2), GxEPD_RED);

        drawArrowDown(kDownArrowCx, kDownArrowTipY);

        drawArrowUp(kUpArrowCx, kUpArrowTipY);

        display.setTextSize(recvTextSize);
        display.setCursor(static_cast<int16_t>(recvTextCursorX),
                          static_cast<int16_t>(kFooterTextTop));
        display.print(recvBuf);

        display.setTextSize(sentTextSize);
        display.setCursor(static_cast<int16_t>(sentTextCursorX),
                          static_cast<int16_t>(kFooterTextTop));
        display.print(sentBuf);

    } while (display.nextPage());

    display.hibernate();
    displaySuspendSpiLowPower();

    ESP_LOGI(TAG, "Rotes Herz mit Zaehlern gezeichnet");
}

void drawAuthPrompt() {
    displayResumeSpiForDraw();

    ESP_LOGI(TAG, "Zeichne Web-Auth Hinweis…");

    static constexpr const char kPrompt[] = "Web Auth?";
    const int                   dw      = display.width();
    const int                   dh      = display.height();

    display.setTextColor(GxEPD_BLACK);
    uint8_t textSize = 3;
    int16_t x1        = 0;
    int16_t y1        = 0;
    uint16_t w        = 0;
    uint16_t h        = 0;
    for (;;) {
        display.setTextSize(textSize);
        display.getTextBounds(kPrompt, 0, 0, &x1, &y1, &w, &h);
        if (static_cast<int>(w) <= dw - 8 && static_cast<int>(h) <= dh - 8) {
            break;
        }
        if (textSize <= 1) {
            break;
        }
        textSize--;
    }

    const int cursorX = (dw - static_cast<int>(w)) / 2 - static_cast<int>(x1);
    const int cursorY = (dh - static_cast<int>(h)) / 2 - static_cast<int>(y1);

    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setCursor(static_cast<int16_t>(cursorX), static_cast<int16_t>(cursorY));
        display.print(kPrompt);
    } while (display.nextPage());

    display.hibernate();
    displaySuspendSpiLowPower();

    ESP_LOGI(TAG, "Web-Auth Hinweis gezeichnet");
}

void drawAuthCode(uint32_t code) {
    displayResumeSpiForDraw();

    ESP_LOGI(TAG, "Zeichne Web-Auth-Code…");

    char digits[8];
    snprintf(digits, sizeof(digits), "%06lu", static_cast<unsigned long>(code % 1000000U));

    const int dw = display.width();
    const int dh = display.height();

    display.setTextColor(GxEPD_BLACK);
    uint8_t textSize = 4;
    int16_t x1 = 0;
    int16_t y1 = 0;
    uint16_t w = 0;
    uint16_t h = 0;
    for (;;) {
        display.setTextSize(textSize);
        display.getTextBounds(digits, 0, 0, &x1, &y1, &w, &h);
        if (static_cast<int>(w) <= dw - 8 && static_cast<int>(h) <= dh - 8) {
            break;
        }
        if (textSize <= 2) {
            break;
        }
        textSize--;
    }

    const int cursorX = (dw - static_cast<int>(w)) / 2 - static_cast<int>(x1);
    const int cursorY = (dh - static_cast<int>(h)) / 2 - static_cast<int>(y1);

    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextSize(textSize);
        display.setCursor(static_cast<int16_t>(cursorX), static_cast<int16_t>(cursorY));
        display.print(digits);
    } while (display.nextPage());

    display.hibernate();
    displaySuspendSpiLowPower();

    ESP_LOGI(TAG, "Auth-Code gezeichnet");
}

void drawSplashScreen() {
    displayResumeSpiForDraw();

    ESP_LOGI(TAG, "Zeichne Splash Chaya2MQTT...");

    static constexpr const char kTitle[] = "Chaya2MQTT";
    const int                   dw = display.width();
    const int                   dh = display.height();

    display.setTextColor(GxEPD_BLACK);
    uint8_t textSize = 3;
    int16_t x1;
    int16_t y1;
    uint16_t w;
    uint16_t h;
    for (;;) {
        display.setTextSize(textSize);
        display.getTextBounds(kTitle, 0, 0, &x1, &y1, &w, &h);
        if (static_cast<int>(w) <= dw - 8 && static_cast<int>(h) <= dh - 8) {
            break;
        }
        if (textSize <= 1) {
            break;
        }
        textSize--;
    }

    const int cursorX = (dw - static_cast<int>(w)) / 2 - static_cast<int>(x1);
    const int cursorY = (dh - static_cast<int>(h)) / 2 - static_cast<int>(y1);

    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setCursor(static_cast<int16_t>(cursorX), static_cast<int16_t>(cursorY));
        display.print(kTitle);
    } while (display.nextPage());

    display.hibernate();
    displaySuspendSpiLowPower();

    ESP_LOGI(TAG, "Splash gezeichnet");
}
