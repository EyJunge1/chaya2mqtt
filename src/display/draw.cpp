#include "display.h"
#include "internal.h"

#include "config/app_config.h"
#include "constants.h"
#include "display_config.h"
#include "heart/counter.h"
#include "heart/counter_pure.h"
#include "wifi/wlan.h"

#include <Arduino.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <esp_log.h>

#include "util/log_tag.h"

DEFINE_LOG_TAG("DISP");

namespace {

uint16_t displayFgColor() {
    return configGetDisplayDark() ? GxEPD_WHITE : GxEPD_BLACK;
}

uint16_t displayBgColor() {
    return configGetDisplayDark() ? GxEPD_BLACK : GxEPD_WHITE;
}

// Centered text; shrink textSize down to minSize if needed.
void drawCenteredTextScreen(const char* text, uint8_t startSize, uint8_t minSize) {
    auto& epd = displayPanel();
    const int dw = epd.width();
    const int dh = epd.height();
    const uint16_t fg = displayFgColor();
    const uint16_t bg = displayBgColor();

    epd.setTextColor(fg);
    uint8_t  textSize = startSize;
    int16_t  x1       = 0;
    int16_t  y1       = 0;
    uint16_t w        = 0;
    uint16_t h        = 0;

    for (;;) {
        epd.setTextSize(textSize);
        epd.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
        if (static_cast<int>(w) <= dw - 8 && static_cast<int>(h) <= dh - 8) {
            break;
        }
        if (textSize <= minSize) {
            break;
        }
        textSize--;
    }

    const int cursorX = (dw - static_cast<int>(w)) / 2 - static_cast<int>(x1);
    const int cursorY = (dh - static_cast<int>(h)) / 2 - static_cast<int>(y1);

    epd.firstPage();
    do {
        epd.fillScreen(bg);
        epd.setTextColor(fg);
        epd.setTextSize(textSize);
        epd.setCursor(static_cast<int16_t>(cursorX), static_cast<int16_t>(cursorY));
        epd.print(text);
    } while (epd.nextPage());
}

// Incoming arrow (tip toward +y).
static void drawArrowDown(int16_t cx, int16_t tipY, uint16_t color) {
    auto& epd = displayPanel();
    static constexpr int16_t kHalf = 12;
    static constexpr int16_t kStemH = 20;
    const int16_t            baseY = static_cast<int16_t>(tipY - 5);
    epd.fillTriangle(cx, tipY, static_cast<int16_t>(cx - kHalf), baseY,
                     static_cast<int16_t>(cx + kHalf), baseY, color);
    epd.fillRect(static_cast<int16_t>(cx - 2), static_cast<int16_t>(tipY - kStemH - 6), 4,
                 kStemH, color);
}

// Outgoing arrow (tip toward −y).
static void drawArrowUp(int16_t cx, int16_t tipY, uint16_t color) {
    auto& epd = displayPanel();
    static constexpr int16_t kHalf = 12;
    static constexpr int16_t kStemH = 20;
    const int16_t            baseY = static_cast<int16_t>(tipY + 5);
    epd.fillTriangle(cx, tipY, static_cast<int16_t>(cx - kHalf), baseY,
                     static_cast<int16_t>(cx + kHalf), baseY, color);
    epd.fillRect(static_cast<int16_t>(cx - 2), static_cast<int16_t>(tipY + 6), 4, kStemH,
                 color);
}

static uint8_t footerTextSizeForDigitCount(size_t digitLen) {
    return digitLen <= 3 ? 4 : 3;
}

static void formatCappedCounterForDisplay(int rawCounter, int baseline, char* buf, size_t buflen) {
    if (heartCounterShouldShowPlusPure(rawCounter, baseline)) {
        static_cast<void>(snprintf(buf, buflen, "%d+", kDisplayCounterMax));
        return;
    }
    static_cast<void>(
        snprintf(buf, buflen, "%d", heartCounterShownDeltaPure(rawCounter, baseline)));
}

} // namespace

void drawHeartWithNumber() {
    displayResumeSpiForDraw();

    ESP_LOGI(TAG, "Drawing red heart with counters...");

    auto& epd = displayPanel();
    const uint16_t fg = displayFgColor();
    const uint16_t bg = displayBgColor();

    static constexpr int kCenterX       = 100;
    static constexpr int kHeartSize       = 70;
    static constexpr int kCircleRadius  = (kHeartSize / 2) + 8;
    static constexpr int kCircleSpacing = (kHeartSize / 2) - 3;
    static constexpr int kCircleY       = 50;
    static constexpr int kTriangleTop   = kCircleY + 15;
    static constexpr int kTriangleBottom = 163;
    static constexpr int kMaxWidth      = 2 * (kCircleSpacing + kCircleRadius) - 4;

    const int dw = epd.width();
    const int dh = epd.height();

    const int16_t triLeftX   = static_cast<int16_t>(kCenterX - (kMaxWidth / 2));
    const int16_t triRightX  = static_cast<int16_t>(kCenterX + (kMaxWidth / 2));
    const int16_t triBottomY = static_cast<int16_t>(kTriangleBottom);

    char recvBuf[16];
    char sentBuf[16];
    HeartCounterDrawSnapshot snap{};
    heartCounterFillDrawSnapshot(&snap);
    formatCappedCounterForDisplay(snap.heartCounterRaw, snap.counterBaselineRaw, recvBuf, sizeof(recvBuf));
    formatCappedCounterForDisplay(snap.heartSentCounterRaw, snap.sentCountBaselineRaw, sentBuf, sizeof(sentBuf));
    const size_t recvLen = std::max<size_t>(strlen(recvBuf), size_t{1});
    const size_t sentLen = std::max<size_t>(strlen(sentBuf), size_t{1});

    const uint8_t recvTextSize = footerTextSizeForDigitCount(recvLen);
    const uint8_t sentTextSize = footerTextSizeForDigitCount(sentLen);

    static constexpr int       kFooterTextTop = 167;
    static constexpr int       kLeftMargin    = 4;
    static constexpr int       kRightMargin   = 4;
    static constexpr int       kArrowLane     = 26;
    static constexpr int16_t   kDownArrowCx   = 13;
    static constexpr int16_t   kDownArrowTipY = 198;
    const int16_t              kUpArrowCx     = static_cast<int16_t>(dw - 13);
    static constexpr int16_t   kUpArrowTipY =
        static_cast<int16_t>(kFooterTextTop + 1);

    int16_t rx1 = 0;
    int16_t ry1 = 0;
    uint16_t rw = 0;
    [[maybe_unused]] uint16_t rh = 0;
    int16_t sx1 = 0;
    int16_t sy1 = 0;
    uint16_t sw = 0;
    [[maybe_unused]] uint16_t sh = 0;

    epd.setTextColor(fg);

    epd.setTextSize(recvTextSize);
    epd.getTextBounds(recvBuf, 0, 0, &rx1, &ry1, &rw, &rh);
    const int recvTextCursorX = kLeftMargin + kArrowLane - static_cast<int>(rx1);

    epd.setTextSize(sentTextSize);
    epd.getTextBounds(sentBuf, 0, 0, &sx1, &sy1, &sw, &sh);
    const int sentTextCursorX =
        dw - kRightMargin - kArrowLane - static_cast<int>(sw) - static_cast<int>(sx1);

    epd.firstPage();
    do {
        epd.fillScreen(bg);

        epd.fillCircle(static_cast<int16_t>(kCenterX - kCircleSpacing),
                         static_cast<int16_t>(kCircleY),
                         static_cast<int16_t>(kCircleRadius), GxEPD_RED);
        epd.fillCircle(static_cast<int16_t>(kCenterX + kCircleSpacing),
                         static_cast<int16_t>(kCircleY),
                         static_cast<int16_t>(kCircleRadius), GxEPD_RED);

        if (kTriangleTop >= 0 && kTriangleBottom < dh && triLeftX >= 0 && triRightX < dw) {
            epd.fillTriangle(triLeftX, static_cast<int16_t>(kTriangleTop), triRightX,
                             static_cast<int16_t>(kTriangleTop),
                             static_cast<int16_t>(kCenterX), triBottomY, GxEPD_RED);
        }

        epd.fillRect(static_cast<int16_t>(kCenterX - (kHeartSize / 3)),
                       static_cast<int16_t>(kCircleY - (kHeartSize / 6)),
                       static_cast<int16_t>((kHeartSize * 2) / 3),
                       static_cast<int16_t>(kHeartSize / 2), GxEPD_RED);

        drawArrowDown(kDownArrowCx, kDownArrowTipY, fg);

        drawArrowUp(kUpArrowCx, kUpArrowTipY, fg);

        epd.setTextColor(fg);
        epd.setTextSize(recvTextSize);
        epd.setCursor(static_cast<int16_t>(recvTextCursorX),
                      static_cast<int16_t>(kFooterTextTop));
        epd.print(recvBuf);

        epd.setTextSize(sentTextSize);
        epd.setCursor(static_cast<int16_t>(sentTextCursorX),
                      static_cast<int16_t>(kFooterTextTop));
        epd.print(sentBuf);

    } while (epd.nextPage());

    epd.hibernate();
    displaySuspendSpiLowPower();

    ESP_LOGI(TAG, "Red heart with counters drawn");
}

void drawSplashScreen() {
    displayResumeSpiForDraw();

    ESP_LOGI(TAG, "Drawing Chaya2MQTT splash...");

    char apSsid[kWifiSsidMaxLen]{};
    char apIp[16]{};
    if (configIsApMode()
        && wlanApSetupSnapshot(apSsid, sizeof(apSsid), apIp, sizeof(apIp))) {
        auto& epd = displayPanel();
        const uint16_t fg = displayFgColor();
        const uint16_t bg = displayBgColor();
        char lineUrl[28]{};
        static_cast<void>(snprintf(lineUrl, sizeof(lineUrl), "http://%s", apIp));

        epd.firstPage();
        do {
            epd.fillScreen(bg);
            epd.setTextColor(fg);
            epd.setTextSize(2);
            epd.setCursor(8, 28);
            epd.print(kSetupApSsid);
            epd.setTextSize(1);
            epd.setCursor(8, 70);
            epd.print(F("WiFi setup"));
            epd.setCursor(8, 92);
            epd.print(apSsid);
            epd.setCursor(8, 114);
            epd.print(lineUrl);
        } while (epd.nextPage());
        epd.hibernate();
        displaySuspendSpiLowPower();
        ESP_LOGI(TAG, "AP splash drawn");
        return;
    }

    static constexpr const char kTitle[] = "Chaya2MQTT";
    drawCenteredTextScreen(kTitle, 3, 1);
    displayPanel().hibernate();
    displaySuspendSpiLowPower();

    ESP_LOGI(TAG, "Splash drawn");
}
