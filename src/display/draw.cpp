#include "display.h"
#include "internal.h"

#include "constants.h"
#include "display_config.h"
#include "draw_pure.h"
#include "heart/counter.h"
#include "heart/counter_pure.h"
#include "hw/battery.h"
#include "hw/pins.h"
#include "wifi/wifi_qr_pure.h"
#include "wifi/wlan.h"
#include "wifi/wlan_config.h"

#include "qr/qrcodegen.h"

#include <Arduino.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <esp_log.h>

#include "util/log_tag.h"

DEFINE_LOG_TAG("DISP");

namespace {

// QR work buffers live in BSS — display task stack is too small for encode + draw locals.
static constexpr int kQrMaxVersion = 3;
static uint8_t s_qrTempBuf[qrcodegen_BUFFER_LEN_FOR_VERSION(kQrMaxVersion)];
static uint8_t s_qrcode[qrcodegen_BUFFER_LEN_FOR_VERSION(kQrMaxVersion)];
static char    s_wifiQrPayload[kWifiQrPayloadMaxLen];

uint16_t displayFgColor() {
    return GxEPD_BLACK;
}

uint16_t displayBgColor() {
    return GxEPD_WHITE;
}

// Centered text; shrink textSize down to minSize if needed.
void drawCenteredTextScreen(const char* text, uint8_t startSize, uint8_t minSize, uint16_t color) {
    auto& epd = displayPanel();
    const int dw = epd.width();
    const int dh = epd.height();
    const uint16_t bg = displayBgColor();

    epd.setTextColor(color);
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

    epd.setFullWindow();
    epd.firstPage();
    do {
        epd.fillScreen(bg);
        epd.setTextColor(color);
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

static void drawBatteryIcon(int16_t x, int16_t y, int pct, uint16_t color) {
    auto& epd = displayPanel();
    static constexpr int16_t kW = 14;
    static constexpr int16_t kH = 8;
    epd.drawRect(x, y, kW, kH, color);
    epd.fillRect(static_cast<int16_t>(x + kW), static_cast<int16_t>(y + 2), 2, 4, color);
    const int fillW = ((pct < 0 ? 0 : (pct > 100 ? 100 : pct)) * (kW - 2)) / 100;
    if (fillW > 0) {
        epd.fillRect(static_cast<int16_t>(x + 1), static_cast<int16_t>(y + 1),
                     static_cast<int16_t>(fillW), static_cast<int16_t>(kH - 2), color);
    }
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

HeartCounterDrawSnapshot drawHeartWithNumber() {
    displayResumeSpiForDraw();

    ESP_LOGI(TAG, "Drawing red heart with counters...");

    auto& epd = displayPanel();
    const uint16_t fg = displayFgColor();
    const uint16_t bg = displayBgColor();

    static constexpr int kCenterX         = 100;
    static constexpr int kHeartSize       = 70;
    static constexpr int kCircleRadius    = (kHeartSize / 2) + 8;
    static constexpr int kCircleSpacing   = (kHeartSize / 2) - 3;
    static constexpr int kCircleY         = 60;
    static constexpr int kTriangleTop     = kCircleY + 15;
    static constexpr int kTriangleBottom  = 173;
    static constexpr int kMaxWidth        = 2 * (kCircleSpacing + kCircleRadius) - 4;

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
    const int     batPct       = batteryPercent();
    uint16_t      batColor     = fg;
    switch (displayBatteryColor(batPct)) {
    case DisplayBatteryColor::Red:
        batColor = GxEPD_RED;
        break;
    case DisplayBatteryColor::Yellow:
        batColor = GxEPD_YELLOW;
        break;
    case DisplayBatteryColor::Black:
        break;
    }

    static constexpr int       kFooterTextTop = 167;
    static constexpr int       kLeftMargin    = 4;
    static constexpr int       kRightMargin   = 4;
    static constexpr int       kArrowLane     = 26;
    static constexpr int16_t   kDownArrowCx   = 13;
    static constexpr int16_t   kDownArrowTipY = 198;
    const int16_t              kUpArrowCx     = static_cast<int16_t>(dw - 13);
    static constexpr int16_t   kUpArrowTipY =
        static_cast<int16_t>(kFooterTextTop + 1);
    static constexpr int16_t   kBatteryWidthWithTerminal = 16;
    static constexpr int16_t   kBatteryTopMargin         = 4;
    const int16_t              kBatteryX =
        static_cast<int16_t>(dw - kRightMargin - kBatteryWidthWithTerminal);

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

    epd.setFullWindow();
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

        drawBatteryIcon(kBatteryX, kBatteryTopMargin, batPct, batColor);

    } while (epd.nextPage());

    epd.hibernate();
    displaySuspendSpiLowPower();

    ESP_LOGI(TAG, "Red heart with counters drawn");
    return snap;
}

DisplayView displaySplashTargetView() {
    char apSsid[kWifiSsidMaxLen]{};
    char apIp[16]{};
    char apPass[kSetupApPassBufLen]{};
    if (configIsApMode()
        && wlanApSetupSnapshot(apSsid, sizeof(apSsid), apIp, sizeof(apIp))
        && wlanApSetupPassSnapshot(apPass, sizeof(apPass))) {
        return DisplayView::SetupQr;
    }
    return DisplayView::ProductTitle;
}

DisplayView drawSplashScreen() {
    displayResumeSpiForDraw();

    ESP_LOGI(TAG, "Drawing Chaya2MQTT splash...");

    char apSsid[kWifiSsidMaxLen]{};
    char apIp[16]{};
    char apPass[kSetupApPassBufLen]{};
    if (configIsApMode()
        && wlanApSetupSnapshot(apSsid, sizeof(apSsid), apIp, sizeof(apIp))
        && wlanApSetupPassSnapshot(apPass, sizeof(apPass))) {
        static_cast<void>(apIp);
        if (!wifiQrBuildWpaPayload(apSsid, apPass, s_wifiQrPayload, sizeof(s_wifiQrPayload))) {
            ESP_LOGE(TAG, "AP WIFI QR payload failed");
            drawCenteredTextScreen(kSetupApSsid, 3, 1, GxEPD_RED);
            displayPanel().hibernate();
            displaySuspendSpiLowPower();
            return DisplayView::ProductTitle;
        }

        // Version 3 (29 modules) holds our ~40-byte MeCard with ECC-M.
        const bool ok = qrcodegen_encodeText(
            s_wifiQrPayload, s_qrTempBuf, s_qrcode, qrcodegen_Ecc_MEDIUM, 1, kQrMaxVersion,
            qrcodegen_Mask_AUTO, true);
        if (!ok) {
            ESP_LOGE(TAG, "AP WIFI QR encode failed");
            drawCenteredTextScreen(kSetupApSsid, 3, 1, GxEPD_RED);
            displayPanel().hibernate();
            displaySuspendSpiLowPower();
            return DisplayView::ProductTitle;
        }

        auto& epd = displayPanel();
        const int dw = epd.width();
        const int dh = epd.height();
        const int qrSize = qrcodegen_getSize(s_qrcode);
        // Quiet zone: 4 modules left/right/bottom (QR standard). Top is the title band.
        static constexpr int kQuiet = 4;
        static constexpr const char kSplashTitle[] = "Chaya2MQTT";
        const int totalMods = qrSize + 2 * kQuiet;
        int scale = std::min(dw, dh) / totalMods;
        if (scale < 1) {
            scale = 1;
        }
        const int drawn   = totalMods * scale;
        const int originX = (dw - drawn) / 2;
        // Sit the modules near the bottom; keep a thin white pad so pixels are not clipped.
        static constexpr int kBottomPadPx = 4;
        const int originY = dh - kBottomPadPx - qrSize * scale;
        const uint16_t dark  = GxEPD_BLACK;
        const uint16_t light = GxEPD_WHITE;

        uint8_t titleSize = 3;
        int16_t tx1 = 0;
        int16_t ty1 = 0;
        uint16_t tw = 0;
        uint16_t th = 0;
        epd.setTextColor(GxEPD_RED);
        for (;;) {
            epd.setTextSize(titleSize);
            epd.getTextBounds(kSplashTitle, 0, 0, &tx1, &ty1, &tw, &th);
            if (static_cast<int>(tw) <= dw - 4 && static_cast<int>(th) + 2 <= originY) {
                break;
            }
            if (titleSize <= 1) {
                break;
            }
            titleSize--;
        }
        const int titleCursorX = (dw - static_cast<int>(tw)) / 2 - static_cast<int>(tx1);
        const int titleCursorY =
            (originY - static_cast<int>(th)) / 2 - static_cast<int>(ty1);

        const uint32_t drawStartedMs = millis();
        epd.setFullWindow();
        epd.firstPage();
        do {
            epd.fillScreen(light);
            epd.setTextColor(GxEPD_RED);
            epd.setTextSize(titleSize);
            epd.setCursor(static_cast<int16_t>(titleCursorX), static_cast<int16_t>(titleCursorY));
            epd.print(kSplashTitle);
            for (int y = 0; y < qrSize; ++y) {
                for (int x = 0; x < qrSize; ++x) {
                    if (!qrcodegen_getModule(s_qrcode, x, y)) {
                        continue;
                    }
                    const int px = originX + (x + kQuiet) * scale;
                    const int py = originY + y * scale;
                    epd.fillRect(static_cast<int16_t>(px), static_cast<int16_t>(py),
                                 static_cast<int16_t>(scale), static_cast<int16_t>(scale), dark);
                }
            }
        } while (epd.nextPage());
        [[maybe_unused]] const uint32_t drawMs = millis() - drawStartedMs;
        epd.hibernate();
        displaySuspendSpiLowPower();
        ESP_LOGI(TAG, "AP WIFI QR splash drawn (modules=%d scale=%d ms=%lu busy=%d)", qrSize,
                 scale, static_cast<unsigned long>(drawMs), digitalRead(pins::kDisplayBusy));
        return DisplayView::SetupQr;
    }

    static constexpr const char kTitle[] = "Chaya2MQTT";
    drawCenteredTextScreen(kTitle, 3, 1, GxEPD_RED);
    displayPanel().hibernate();
    displaySuspendSpiLowPower();

    ESP_LOGI(TAG, "Splash drawn");
    return DisplayView::ProductTitle;
}

void drawPowerOffScreen() {
    displayResumeSpiForDraw();

    ESP_LOGI(TAG, "Drawing power-off screen...");
    static constexpr const char kTitle[] = "Chaya2MQTT";
    drawCenteredTextScreen(kTitle, 3, 1, GxEPD_RED);
    displayPanel().hibernate();
    displaySuspendSpiLowPower();
    ESP_LOGI(TAG, "Power-off screen drawn");
}
