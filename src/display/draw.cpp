#include "display.h"
#include "internal.h"

#include "constants.h"
#include "display_config.h"
#include "draw_pure.h"
#include "heart/counter.h"
#include "heart/counter_pure.h"
#include "battery/battery.h"
#include "hw/pins.h"
#include "icons_lucide.h"
#include "wifi/wifi_qr_pure.h"
#include "wifi/wlan.h"
#include "wifi/wlan_config.h"

#include "qr/qrcodegen.h"

#include <Arduino.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <esp_log.h>

#include "util/log_tag.h"

DEFINE_LOG_TAG("DISP");

namespace {

// QR work buffers live in BSS — display task stack is too small for encode + draw locals.
// Setup AP WIFI MeCard is ~52 bytes (SSID + 24-char PSK). Version 3-M holds 42 bytes;
// version 4-M holds 62. 1.54" 200×200 still scales to 4 px/module with quiet zone.
static constexpr int kQrMaxVersion = 4;
static uint8_t s_qrTempBuf[qrcodegen_BUFFER_LEN_FOR_VERSION(kQrMaxVersion)];
static uint8_t s_qrcode[qrcodegen_BUFFER_LEN_FOR_VERSION(kQrMaxVersion)];
static char    s_wifiQrPayload[kWifiQrPayloadMaxLen];

static constexpr int kDisplayRightMargin = 4;
static constexpr int16_t kBatteryTopMargin = 4;

uint16_t displayFgColor() {
    return GxEPD_BLACK;
}

uint16_t displayBgColor() {
    return GxEPD_WHITE;
}

static void drawLucideIcon(int16_t x, int16_t y, const uint8_t* bitmap, int16_t w, int16_t h,
                           uint16_t color) {
    displayPanel().drawBitmap(x, y, bitmap, w, h, color);
}

struct BatteryIconRow {
    DisplayBatteryIcon icon;
    const uint8_t*     bitmap;
    int16_t            w;
    int16_t            h;
};

static constexpr BatteryIconRow kBatteryIconRows[] = {
    {DisplayBatteryIcon::Full, kIconBatteryFull, kIconBatteryFullW, kIconBatteryFullH},
    {DisplayBatteryIcon::Medium, kIconBatteryMedium, kIconBatteryMediumW, kIconBatteryMediumH},
    {DisplayBatteryIcon::Low, kIconBatteryLow, kIconBatteryLowW, kIconBatteryLowH},
    {DisplayBatteryIcon::Empty, kIconBatteryEmpty, kIconBatteryEmptyW, kIconBatteryEmptyH},
};

struct HeartIconRow {
    DisplayHeartIcon icon;
    const uint8_t*   bitmap;
    int16_t          w;
    int16_t          h;
};

static constexpr HeartIconRow kHeartIconRows[] = {
    {DisplayHeartIcon::Filled, kIconHeart, kIconHeartW, kIconHeartH},
    {DisplayHeartIcon::Crack, kIconHeartCrack, kIconHeartCrackW, kIconHeartCrackH},
};

template <typename Row, typename Icon, size_t N>
static const Row* findIconRow(const Row (&table)[N], Icon icon) {
    for (const Row& row : table) {
        if (row.icon == icon) {
            return &row;
        }
    }
    return nullptr;
}

static uint8_t footerTextSizeForDigitCount(size_t digitLen) {
    return digitLen <= 3 ? 4 : 3;
}

static void drawBatteryLucide(int16_t x, int16_t y, int pct, uint16_t color) {
    const DisplayBatteryIcon icon = displayBatteryIcon(pct);
    if (const BatteryIconRow* row = findIconRow(kBatteryIconRows, icon)) {
        drawLucideIcon(x, y, row->bitmap, row->w, row->h, color);
    }
}

static bool splashApSetupSnapshot(char* ssid, size_t ssidLen, char* ip, size_t ipLen, char* pass,
                                  size_t passLen) {
    return configIsApMode() && wlanApSetupSnapshot(ssid, ssidLen, ip, ipLen)
           && wlanApSetupPassSnapshot(pass, passLen);
}

// Same top-right placement as the RX/TX heart counter view.
static void drawBatteryTopRight() {
    auto& epd = displayPanel();
    const int batPct = batteryPercent();
    uint16_t batColor = displayFgColor();
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
    const int16_t batteryX =
        static_cast<int16_t>(epd.width() - kDisplayRightMargin - kIconBatteryFullW);
    drawBatteryLucide(batteryX, kBatteryTopMargin, batPct, batColor);
}

// Centered text; shrink textSize down to minSize if needed.
void drawCenteredTextScreen(const char* text, uint8_t startSize, uint8_t minSize, uint16_t color,
                            bool showBattery) {
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
        if (showBattery) {
            drawBatteryTopRight();
        }
    } while (epd.nextPage());
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

HeartCounterDrawSnapshot drawHeartWithNumber(DisplayHeartIcon icon) {
    displayResumeSpiForDraw();

    const bool showFooter = (icon != DisplayHeartIcon::Crack);
    ESP_LOGI(TAG, "Drawing Lucide heart (icon=%u showFooter=%d)...",
             static_cast<unsigned>(icon), showFooter ? 1 : 0);

    auto& epd = displayPanel();
    const uint16_t fg = displayFgColor();
    const uint16_t bg = displayBgColor();
    const int dw = epd.width();
    const int dh = epd.height();

    HeartCounterDrawSnapshot snap{};
    heartCounterFillDrawSnapshot(&snap);

    char recvBuf[16]{};
    char sentBuf[16]{};
    uint8_t recvTextSize = 4;
    uint8_t sentTextSize = 4;
    int recvTextCursorX = 0;
    int sentTextCursorX = 0;

    static constexpr int kFooterTextTop = 167;
    static constexpr int kLeftMargin    = 4;
    // 14 px move icon + exactly 5 px gap to the counter.
    static constexpr int kArrowLane = kIconMoveDownW + 5;
    static constexpr int16_t kHeartFooterGap = 4;

    const HeartIconRow* heartRow = findIconRow(kHeartIconRows, icon);
    if (heartRow == nullptr) {
        heartRow = &kHeartIconRows[0];
    }
    const int16_t heartW   = heartRow->w;
    const int16_t heartH   = heartRow->h;
    const uint8_t* heartBmp = heartRow->bitmap;
    const int16_t heartX = static_cast<int16_t>((dw - heartW) / 2);
    const int16_t heartY =
        showFooter ? static_cast<int16_t>(kFooterTextTop - heartH - kHeartFooterGap)
                   : static_cast<int16_t>((dh - heartH) / 2);

    const int16_t downArrowX = static_cast<int16_t>(kLeftMargin);
    const int16_t downArrowY = static_cast<int16_t>(kFooterTextTop);
    const int16_t upArrowX =
        static_cast<int16_t>(dw - kDisplayRightMargin - kIconMoveUpW);
    const int16_t upArrowY = static_cast<int16_t>(kFooterTextTop);

    if (showFooter) {
        formatCappedCounterForDisplay(snap.heartCounterRaw, snap.counterBaselineRaw, recvBuf,
                                      sizeof(recvBuf));
        formatCappedCounterForDisplay(snap.heartSentCounterRaw, snap.sentCountBaselineRaw, sentBuf,
                                      sizeof(sentBuf));
        const size_t recvLen = std::max<size_t>(strlen(recvBuf), size_t{1});
        const size_t sentLen = std::max<size_t>(strlen(sentBuf), size_t{1});
        recvTextSize = footerTextSizeForDigitCount(recvLen);
        sentTextSize = footerTextSizeForDigitCount(sentLen);

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
        recvTextCursorX = kLeftMargin + kArrowLane - static_cast<int>(rx1);

        epd.setTextSize(sentTextSize);
        epd.getTextBounds(sentBuf, 0, 0, &sx1, &sy1, &sw, &sh);
        // The classic GFX font reports one trailing blank column per text size.
        // Compensate it so the visible rightmost digit is also 5 px from the icon.
        const int sentTrailingBlankPx = sentTextSize;
        sentTextCursorX = dw - kDisplayRightMargin - kArrowLane - static_cast<int>(sw)
                          - static_cast<int>(sx1) + sentTrailingBlankPx;
    }

    epd.setFullWindow();
    epd.firstPage();
    do {
        epd.fillScreen(bg);

        drawLucideIcon(heartX, heartY, heartBmp, heartW, heartH, GxEPD_RED);

        if (showFooter) {
            drawLucideIcon(downArrowX, downArrowY, kIconMoveDown, kIconMoveDownW, kIconMoveDownH,
                           fg);
            drawLucideIcon(upArrowX, upArrowY, kIconMoveUp, kIconMoveUpW, kIconMoveUpH, fg);

            epd.setTextColor(fg);
            epd.setTextSize(recvTextSize);
            epd.setCursor(static_cast<int16_t>(recvTextCursorX),
                          static_cast<int16_t>(kFooterTextTop));
            epd.print(recvBuf);

            epd.setTextSize(sentTextSize);
            epd.setCursor(static_cast<int16_t>(sentTextCursorX),
                          static_cast<int16_t>(kFooterTextTop));
            epd.print(sentBuf);
        }

        drawBatteryTopRight();

    } while (epd.nextPage());

    epd.hibernate();
    displaySuspendSpiLowPower();

    ESP_LOGI(TAG, "Lucide heart drawn (showFooter=%d)", showFooter ? 1 : 0);
    return snap;
}

DisplayView displaySplashTargetView() {
    char apSsid[kWifiSsidMaxLen]{};
    char apIp[16]{};
    char apPass[kSetupApPassBufLen]{};
    if (splashApSetupSnapshot(apSsid, sizeof(apSsid), apIp, sizeof(apIp), apPass, sizeof(apPass))) {
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
    if (splashApSetupSnapshot(apSsid, sizeof(apSsid), apIp, sizeof(apIp), apPass, sizeof(apPass))) {
        static_cast<void>(apIp);
        if (!wifiQrBuildWpaPayload(apSsid, apPass, s_wifiQrPayload, sizeof(s_wifiQrPayload))) {
            ESP_LOGE(TAG, "AP WIFI QR payload failed");
            drawCenteredTextScreen(kSetupApSsid, 3, 1, GxEPD_RED, true);
            displayPanel().hibernate();
            displaySuspendSpiLowPower();
            return DisplayView::ProductTitle;
        }

        const bool ok = qrcodegen_encodeText(
            s_wifiQrPayload, s_qrTempBuf, s_qrcode, qrcodegen_Ecc_MEDIUM, 1, kQrMaxVersion,
            qrcodegen_Mask_AUTO, true);
        if (!ok) {
            ESP_LOGE(TAG, "AP WIFI QR encode failed (len=%u)",
                     static_cast<unsigned>(std::strlen(s_wifiQrPayload)));
            drawCenteredTextScreen(kSetupApSsid, 3, 1, GxEPD_RED, true);
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
            // SoftAP/setup QR splash intentionally omits the battery icon.
        } while (epd.nextPage());
        [[maybe_unused]] const uint32_t drawMs = millis() - drawStartedMs;
        epd.hibernate();
        displaySuspendSpiLowPower();
        ESP_LOGI(TAG, "AP WIFI QR splash drawn (modules=%d scale=%d ms=%lu busy=%d)", qrSize,
                 scale, static_cast<unsigned long>(drawMs), digitalRead(pins::kDisplayBusy));
        return DisplayView::SetupQr;
    }

    static constexpr const char kTitle[] = "Chaya2MQTT";
    drawCenteredTextScreen(kTitle, 3, 1, GxEPD_RED, true);
    displayPanel().hibernate();
    displaySuspendSpiLowPower();

    ESP_LOGI(TAG, "Splash drawn");
    return DisplayView::ProductTitle;
}

void drawPowerOffScreen() {
    displayResumeSpiForDraw();

    ESP_LOGI(TAG, "Drawing power-off Lucide heart-off...");
    auto& epd = displayPanel();
    const uint16_t bg = displayBgColor();
    static constexpr char kTitle[] = "Chaya2MQTT";
    // Same size as the configured-Wi-Fi startup title, with equal outer/title gaps.
    static constexpr uint8_t kTitleSize = 3;
    static constexpr int16_t kPowerOffGap = 10;
    // Lucide heart-off's diagonal slash starts ~7 px above the actual heart outline.
    static constexpr int16_t kHeartOffVisualTopInset = 7;
    static constexpr int16_t kTitleTop = kPowerOffGap;

    int16_t titleX1 = 0;
    int16_t titleY1 = 0;
    uint16_t titleW = 0;
    uint16_t titleH = 0;
    epd.setTextSize(kTitleSize);
    epd.getTextBounds(kTitle, 0, 0, &titleX1, &titleY1, &titleW, &titleH);
    const int16_t titleX =
        static_cast<int16_t>((epd.width() - static_cast<int>(titleW)) / 2 - titleX1);
    const int16_t heartX = static_cast<int16_t>((epd.width() - kIconHeartOffW) / 2);
    const int16_t heartY =
        static_cast<int16_t>(kTitleTop + static_cast<int16_t>(titleH) + kPowerOffGap
                             - kHeartOffVisualTopInset);

    epd.setFullWindow();
    epd.firstPage();
    do {
        epd.fillScreen(bg);
        epd.setTextColor(GxEPD_RED);
        epd.setTextSize(kTitleSize);
        epd.setCursor(titleX, kTitleTop);
        epd.print(kTitle);
        drawLucideIcon(heartX, heartY, kIconHeartOff, kIconHeartOffW, kIconHeartOffH,
                       GxEPD_BLACK);
    } while (epd.nextPage());

    epd.hibernate();
    displaySuspendSpiLowPower();
    ESP_LOGI(TAG, "Power-off screen drawn");
}
