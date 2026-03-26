#include "display.h"

#include "config.h"

#include <Arduino.h>
#include <SPI.h>
#include <cstdio>
#include <cstring>

GxEPD2_3C<GxEPD2_154_Z90c, GxEPD2_154_Z90c::HEIGHT> display(
    GxEPD2_154_Z90c(/*CS=*/ 15, /*DC=*/ 27, /*RST=*/ 26, /*BUSY=*/ 25));

static bool g_heartRedrawPending = false;

#if defined(CORE_DEBUG_LEVEL) && CORE_DEBUG_LEVEL > 0
#define DISPLAY_DBG_PRINTLN(x) Serial.println(x)
#else
#define DISPLAY_DBG_PRINTLN(x) ((void)0)
#endif

void requestHeartRedraw() {
    g_heartRedrawPending = true;
}

bool consumeHeartRedraw() {
    if (!g_heartRedrawPending) {
        return false;
    }
    g_heartRedrawPending = false;
    return true;
}

void displayInit() {
    SPI.begin(/*SCK=*/ 13, /*MISO=*/ 12, /*MOSI=*/ 14, /*SS=*/ 15);
    display.init(115200, true, 2, false);
}

void drawHeartWithNumber() {
    DISPLAY_DBG_PRINTLN("Zeichne rotes Herz mit Zahl...");

    static constexpr int kCenterX = 100;
    static constexpr int kCenterY = 65;
    static constexpr int kHeartSize = 70;
    static constexpr int kCircleRadius = (kHeartSize / 2) + 4;
    static constexpr int kCircleY = kCenterY - (kHeartSize / 3);
    static constexpr int kCircleSpacing = (kHeartSize / 2) - 3;
    static constexpr int kTriangleTop = kCenterY - 2;
    static constexpr int kTriangleBottom = kCenterY + kHeartSize + 20;
    static constexpr int kMaxWidth = kHeartSize + 61;

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

        const int dw = display.width();
        const int dh = display.height();
        const int16_t triLeftX = static_cast<int16_t>(kCenterX - (kMaxWidth / 2));
        const int16_t triRightX = static_cast<int16_t>(kCenterX + (kMaxWidth / 2));
        const int16_t triBottomY = static_cast<int16_t>(kTriangleBottom);
        if (kTriangleTop >= 0 && kTriangleBottom < dh && triLeftX >= 0 && triRightX < dw) {
            display.fillTriangle(triLeftX, static_cast<int16_t>(kTriangleTop), triRightX,
                                 static_cast<int16_t>(kTriangleTop), static_cast<int16_t>(kCenterX), triBottomY,
                                 GxEPD_RED);
        }

        display.fillRect(static_cast<int16_t>(kCenterX - (kHeartSize / 3)),
                         static_cast<int16_t>(kCircleY - (kHeartSize / 6)),
                         static_cast<int16_t>((kHeartSize * 2) / 3),
                         static_cast<int16_t>(kHeartSize / 2), GxEPD_RED);

        char numberBuf[16];
        snprintf(numberBuf, sizeof(numberBuf), "%d", counter);
        const size_t digitLen = strlen(numberBuf);
        uint8_t textSize = 4;
        if (digitLen >= 7) {
            textSize = 2;
        } else if (digitLen >= 5) {
            textSize = 3;
        }

        int16_t x1;
        int16_t y1;
        uint16_t w;
        uint16_t h;
        display.setTextColor(GxEPD_BLACK);
        display.setTextSize(textSize);
        display.getTextBounds(numberBuf, 0, 0, &x1, &y1, &w, &h);

        int textX = kCenterX - static_cast<int>(w / 2);
        static constexpr int kTextY = 165;

        display.setCursor(static_cast<int16_t>(textX), static_cast<int16_t>(kTextY));
        display.print(numberBuf);

    } while (display.nextPage());

    DISPLAY_DBG_PRINTLN("Rotes Herz mit Zahl gezeichnet!");
}
