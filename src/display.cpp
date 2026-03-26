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

        int circleRadius = heartSize / 2 + 4;
        int circleY = centerY - heartSize / 3;
        int circleSpacing = heartSize / 2 - 3;

        display.fillCircle(centerX - circleSpacing, circleY, circleRadius, GxEPD_RED);
        display.fillCircle(centerX + circleSpacing, circleY, circleRadius, GxEPD_RED);

        int triangleTop = centerY - 2;
        int triangleBottom = centerY + heartSize + 20;

        for (int y = triangleTop; y <= triangleBottom; y++) {
            int progress = y - triangleTop;
            int maxWidth = heartSize + 61;
            int currentWidth = maxWidth - (progress * maxWidth / (triangleBottom - triangleTop));

            int leftX = centerX - currentWidth / 2;
            int rightX = centerX + currentWidth / 2;

            if (y < 200 && leftX >= 0 && rightX < 200) {
                display.drawLine(leftX, y, rightX, y, GxEPD_RED);
            }
        }

        display.fillRect(centerX - heartSize / 3, circleY - heartSize / 6, heartSize * 2 / 3,
                         heartSize / 2, GxEPD_RED);

        String numberStr = String(counter);
        int16_t x1, y1;
        uint16_t w, h;
        display.getTextBounds(numberStr, 0, 0, &x1, &y1, &w, &h);

        int textX = centerX - w / 2 - 6;
        int textY = 165;

        display.setTextColor(GxEPD_BLACK);
        display.setTextSize(4);
        display.setCursor(textX, textY);
        display.print(numberStr);

    } while (display.nextPage());

    Serial.println("Rotes Herz mit Zahl gezeichnet!");
}
