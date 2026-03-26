#include "display.h"

#include <Arduino.h>
#include <SPI.h>

GxEPD2_3C<GxEPD2_154_Z90c, GxEPD2_154_Z90c::HEIGHT> display(
    GxEPD2_154_Z90c(/*CS=*/ 15, /*DC=*/ 27, /*RST=*/ 26, /*BUSY=*/ 25));

int counter = 0;

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

        int triangleTop = centerY - 2;
        int triangleBottom = centerY + heartSize + 20;

        for (int y = triangleTop; y <= triangleBottom; y++) {
            int progress = y - triangleTop;
            int maxWidth = heartSize + 61;
            int currentWidth = maxWidth - (progress * maxWidth / (triangleBottom - triangleTop));

            int leftX = centerX - (currentWidth / 2);
            int rightX = centerX + (currentWidth / 2);

            if (y < 200 && leftX >= 0 && rightX < 200) {
                display.drawLine(static_cast<int16_t>(leftX), static_cast<int16_t>(y),
                                 static_cast<int16_t>(rightX), static_cast<int16_t>(y),
                                 GxEPD_RED);
            }
        }

        display.fillRect(static_cast<int16_t>(centerX - (heartSize / 3)),
                         static_cast<int16_t>(circleY - (heartSize / 6)),
                         static_cast<int16_t>((heartSize * 2) / 3),
                         static_cast<int16_t>(heartSize / 2), GxEPD_RED);

        String numberStr = String(counter);
        int16_t x1;
        int16_t y1;
        uint16_t w;
        uint16_t h;
        display.getTextBounds(numberStr, 0, 0, &x1, &y1, &w, &h);

        int textX = centerX - (w / 2) - 6;
        int textY = 165;

        display.setTextColor(GxEPD_BLACK);
        display.setTextSize(4);
        display.setCursor(static_cast<int16_t>(textX), static_cast<int16_t>(textY));
        display.print(numberStr);

    } while (display.nextPage());

    Serial.println("Rotes Herz mit Zahl gezeichnet!");
}
