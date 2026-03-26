#include "display.h"

#include "config.h"

#include <Arduino.h>
#include <SPI.h>
#include <cstdio>

GxEPD2_3C<GxEPD2_154_Z90c, GxEPD2_154_Z90c::HEIGHT> display(
    GxEPD2_154_Z90c(/*CS=*/ 15, /*DC=*/ 27, /*RST=*/ 26, /*BUSY=*/ 25));

static bool g_heartRedrawPending = false;

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
    Serial.println("Zeichne rotes Herz mit Zahl...");

    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);

        int centerX = 100;
        int centerY = 65;
        int heartSize = 70;

        int circleRadius = (heartSize / 2) + 4;
        int circleY = centerY - (heartSize / 3);
        int circleSpacing = (heartSize / 2) - 3;

        display.fillCircle(static_cast<int16_t>(centerX - circleSpacing),
                           static_cast<int16_t>(circleY),
                           static_cast<int16_t>(circleRadius), GxEPD_RED);
        display.fillCircle(static_cast<int16_t>(centerX + circleSpacing),
                           static_cast<int16_t>(circleY),
                           static_cast<int16_t>(circleRadius), GxEPD_RED);

        const int triangleTop = centerY - 2;
        const int triangleBottom = centerY + heartSize + 20;
        const int maxWidth = heartSize + 61;
        const int dw = display.width();
        const int dh = display.height();
        const int16_t triLeftX = static_cast<int16_t>(centerX - (maxWidth / 2));
        const int16_t triRightX = static_cast<int16_t>(centerX + (maxWidth / 2));
        const int16_t triBottomY = static_cast<int16_t>(triangleBottom);
        if (triangleTop >= 0 && triangleBottom < dh && triLeftX >= 0 && triRightX < dw) {
            display.fillTriangle(triLeftX, static_cast<int16_t>(triangleTop), triRightX,
                                 static_cast<int16_t>(triangleTop), static_cast<int16_t>(centerX), triBottomY,
                                 GxEPD_RED);
        }

        display.fillRect(static_cast<int16_t>(centerX - (heartSize / 3)),
                         static_cast<int16_t>(circleY - (heartSize / 6)),
                         static_cast<int16_t>((heartSize * 2) / 3),
                         static_cast<int16_t>(heartSize / 2), GxEPD_RED);

        char numberBuf[16];
        snprintf(numberBuf, sizeof(numberBuf), "%d", counter);
        int16_t x1;
        int16_t y1;
        uint16_t w;
        uint16_t h;
        display.setTextColor(GxEPD_BLACK);
        display.setTextSize(4);
        display.getTextBounds(numberBuf, 0, 0, &x1, &y1, &w, &h);

        int textX = centerX - static_cast<int>(w / 2);
        int textY = 165;

        display.setCursor(static_cast<int16_t>(textX), static_cast<int16_t>(textY));
        display.print(numberBuf);

    } while (display.nextPage());

    Serial.println("Rotes Herz mit Zahl gezeichnet!");
}
