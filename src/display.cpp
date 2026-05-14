#include "display.h"

#include "config.h"

#include <GxEPD2_3C.h>
#include <Arduino.h>
#include <SPI.h>
#include <algorithm>
#include <atomic>
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

/** Gleiche Zuordnung wie in displayInit() / GxEPD2-Konstruktor (SPI + CS). */
static constexpr int kSpiSck   = 13;
static constexpr int kSpiMiso  = 12;
static constexpr int kSpiMosi  = 14;
static constexpr int kSpiCs    = 15;

static GxEPD2_3C<GxEPD2_154_Z90c, GxEPD2_154_Z90c::HEIGHT> display(
    GxEPD2_154_Z90c(/*CS=*/ kSpiCs, /*DC=*/ 27, /*RST=*/ 26, /*BUSY=*/ 25));

// BUSY/RST/DC: pinModes setzt main pinsInit() vor displayInit(); GxEPD2 nutzt diese vor init.

static std::atomic<bool> g_heartRedrawPending{false};
static bool                g_displaySpiSuspendedLowPower = false;

void requestHeartRedraw() {
    g_heartRedrawPending.store(true, std::memory_order_release);
}

bool consumeHeartRedraw() {
    return g_heartRedrawPending.exchange(false, std::memory_order_acq_rel);
}

static void displayResumeSpiForDraw() {
    if (g_displaySpiSuspendedLowPower) {
        gpio_hold_dis(static_cast<gpio_num_t>(kSpiCs));
        g_displaySpiSuspendedLowPower = false;
    }
    SPI.begin(/*SCK=*/ kSpiSck, /*MISO=*/ kSpiMiso, /*MOSI=*/ kSpiMosi, /*SS=*/ kSpiCs);
}

/** Nach Hibernate: SPI freigeben, Datenleitungen pullen; CS HIGH halten (Hold) fuer Light-Sleep. */
static void displaySuspendSpiLowPower() {
    SPI.end();
    pinMode(kSpiSck, INPUT_PULLDOWN);
    pinMode(kSpiMosi, INPUT_PULLDOWN);
    pinMode(kSpiMiso, INPUT_PULLDOWN);
    pinMode(kSpiCs, OUTPUT);
    digitalWrite(kSpiCs, HIGH);
    gpio_hold_en(static_cast<gpio_num_t>(kSpiCs));
    g_displaySpiSuspendedLowPower = true;
}

void displayInit() {
    SPI.begin(/*SCK=*/ kSpiSck, /*MISO=*/ kSpiMiso, /*MOSI=*/ kSpiMosi, /*SS=*/ kSpiCs);
#if defined(CORE_DEBUG_LEVEL) && CORE_DEBUG_LEVEL > 0
    display.init(115200, true, 2, false);
#else
    display.init(0, true, 2, false);
#endif
}

/** Small ↓ (incoming): tip points toward larger y. */
static void drawArrowDown(int16_t cx, int16_t tipY) {
    static constexpr int16_t kHalf = 4;
    static constexpr int16_t kStemH = 8;
    const int16_t baseY = static_cast<int16_t>(tipY - 5);
    display.fillTriangle(cx, tipY, static_cast<int16_t>(cx - kHalf), baseY,
                         static_cast<int16_t>(cx + kHalf), baseY, GxEPD_BLACK);
    display.fillRect(static_cast<int16_t>(cx - 1), static_cast<int16_t>(tipY - kStemH - 6),
                     3, kStemH, GxEPD_BLACK);
}

/** Small ↑ (outgoing): tip points toward smaller y. */
static void drawArrowUp(int16_t cx, int16_t tipY) {
    static constexpr int16_t kHalf = 4;
    static constexpr int16_t kStemH = 8;
    const int16_t baseY = static_cast<int16_t>(tipY + 5);
    display.fillTriangle(cx, tipY, static_cast<int16_t>(cx - kHalf), baseY,
                         static_cast<int16_t>(cx + kHalf), baseY, GxEPD_BLACK);
    display.fillRect(static_cast<int16_t>(cx - 1), static_cast<int16_t>(tipY + 6), 3, kStemH,
                     GxEPD_BLACK);
}

static uint8_t footerTextSizeForDigitCount(size_t digitLen) {
    if (digitLen >= 3) {
        return 1;
    }
    return 2;
}

void drawHeartWithNumber() {
    displayResumeSpiForDraw();

    ESP_LOGI(TAG, "Zeichne rotes Herz mit Zaehlern...");

    static constexpr int kCenterX     = 100;
    /** Vertical position: leaves room for corner counters (-footer band). */
    static constexpr int kCenterY    = 68;
    static constexpr int kHeartSize  = 80;
    static constexpr int kCircleRadius = (kHeartSize / 2) + 4;
    static constexpr int kCircleY = kCenterY - (kHeartSize / 3);
    static constexpr int kCircleSpacing = (kHeartSize / 2) - 3;
    static constexpr int kTriangleTop = kCenterY - 2;
    static constexpr int kTriangleBottom = kCenterY + kHeartSize + 20;
    static constexpr int kMaxWidth = kHeartSize + 61;

    const int dw = display.width();
    const int dh = display.height();

    const int16_t triLeftX   = static_cast<int16_t>(kCenterX - (kMaxWidth / 2));
    const int16_t triRightX  = static_cast<int16_t>(kCenterX + (kMaxWidth / 2));
    const int16_t triBottomY = static_cast<int16_t>(kTriangleBottom);

    char recvBuf[16];
    char sentBuf[16];
    static_cast<void>(snprintf(recvBuf, sizeof(recvBuf), "%d", heartCounter));
    static_cast<void>(snprintf(sentBuf, sizeof(sentBuf), "%d", heartSentCounter));
    const size_t recvLen = std::max<size_t>(strlen(recvBuf), size_t{1});
    const size_t sentLen = std::max<size_t>(strlen(sentBuf), size_t{1});

    const uint8_t recvTextSize = footerTextSizeForDigitCount(recvLen);
    const uint8_t sentTextSize = footerTextSizeForDigitCount(sentLen);

    static constexpr int kFooterBaselineY = 188;
    static constexpr int kLeftMargin       = 4;
    static constexpr int kRightMargin      = 4;
    static constexpr int kArrowLane        = 14;
    static constexpr int16_t kDownArrowCx    = 9;
    static constexpr int16_t kDownArrowTipY = static_cast<int16_t>(kFooterBaselineY - 2);
    const int16_t           kUpArrowCx      = static_cast<int16_t>(dw - 9);
    const int16_t kUpArrowTipY = static_cast<int16_t>(kFooterBaselineY - 22);

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
                          static_cast<int16_t>(kFooterBaselineY));
        display.print(recvBuf);

        display.setTextSize(sentTextSize);
        display.setCursor(static_cast<int16_t>(sentTextCursorX),
                          static_cast<int16_t>(kFooterBaselineY));
        display.print(sentBuf);

    } while (display.nextPage());

    display.hibernate();
    displaySuspendSpiLowPower();

    ESP_LOGI(TAG, "Rotes Herz mit Zaehlern gezeichnet");
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
