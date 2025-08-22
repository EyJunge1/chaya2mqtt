
#include <GxEPD2_BW.h>
#include <GxEPD2_3C.h>
#include <Fonts/FreeMonoBold9pt7b.h>

// Waveshare ESP32 Driver Board Pins (wie im Beispiel)
// BUSY -> 25, RST -> 26, DC -> 27, CS-> 15, CLK -> 13, DIN -> 14

// 1.54" 3-Farben Display (wie im Beispiel)
GxEPD2_3C<GxEPD2_154_Z90c, GxEPD2_154_Z90c::HEIGHT> display(GxEPD2_154_Z90c(/*CS=*/ 15, /*DC=*/ 27, /*RST=*/ 26, /*BUSY=*/ 25));

// Funktion zum Zeichnen eines Herzens
void drawHeart() {
    Serial.println("Zeichne rotes Herz...");
    
    display.setFullWindow();
    display.firstPage();
    do {
        // Weißer Hintergrund
        display.fillScreen(GxEPD_WHITE);
        
        // Herz-Parameter für 200x200 Display
        int centerX = 100;  // Mitte X (200/2)
        int centerY = 75;   // 5 Pixel nach unten für perfekte Zentrierung
        int heartSize = 80; // Noch größeres Herz für optimale Sichtbarkeit
        
        // Zeichne das Herz mit einer besseren Methode
        // Obere Hälfte: Zwei Kreise näher zusammen und größer
        int circleRadius = heartSize/2 + 4;  // 4 Pixel größer für schönere Kreise
        int circleY = centerY - heartSize/3;
        int circleSpacing = heartSize/2 - 3;  // 3 Pixel näher zusammen
        
        // Linker oberer Kreis
        display.fillCircle(centerX - circleSpacing, circleY, circleRadius, GxEPD_RED);
        
        // Rechter oberer Kreis  
        display.fillCircle(centerX + circleSpacing, circleY, circleRadius, GxEPD_RED);
        
        // Untere Hälfte: Extra breites Dreieck für das noch größere Herz
        int triangleTop = centerY - 1;  // 1 Pixel höher für bessere Harmonie
        int triangleBottom = centerY + heartSize + 35;  // Größer nach unten für größeres Herz
        
        // Zeichne das Dreieck Zeile für Zeile
        for (int y = triangleTop; y <= triangleBottom; y++) {
            int progress = y - triangleTop;
            int maxWidth = heartSize + 69;  // 1 Pixel schmaler für bessere Proportionen
            int currentWidth = maxWidth - (progress * maxWidth / (triangleBottom - triangleTop));
            
            int leftX = centerX - currentWidth/2;
            int rightX = centerX + currentWidth/2;
            
            if (y < 200 && leftX >= 0 && rightX < 200) {
                display.drawLine(leftX, y, rightX, y, GxEPD_RED);
            }
        }
        
        // Fülle die Lücken zwischen den Kreisen - größer
        display.fillRect(centerX - heartSize/3, circleY - heartSize/6, heartSize*2/3, heartSize/2, GxEPD_RED);
        
    } while (display.nextPage());
    
    Serial.println("Rotes Herz gezeichnet!");
}

void setup() {
    Serial.begin(115200);
    Serial.println("=== ESP32 Rotes Herz Display ===");
    
    // SPI auf die Waveshare ESP32 Driver Board Pins setzen
    SPI.begin(/*SCK=*/ 13, /*MISO=*/ 12, /*MOSI=*/ 14, /*SS=*/ 15);
    
    // Display initialisieren
    display.init(115200, true, 2, false);
    
    Serial.println("Display initialisiert...");
    
    // Zeichne das rote Herz
    drawHeart();
    
    Serial.println("Setup abgeschlossen");
}

void loop() {
    delay(1000);
}
