
#include <GxEPD2_BW.h>
#include <GxEPD2_3C.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <SPI.h>

// WiFi Konfiguration
const char* ssid = "REDACTED_SSID";
const char* password = "***REMOVED***";

// HiveMQ Konfiguration (TLS)
const char* mqtt_server = "***REMOVED***";
const int mqtt_port = 8883;
const char* mqtt_username = "***REMOVED***";
const char* mqtt_password = "***REMOVED***";
const char* mqtt_topic = "esp32/heart_counter";

// Waveshare ESP32 Driver Board Pins
GxEPD2_3C<GxEPD2_154_Z90c, GxEPD2_154_Z90c::HEIGHT> display(GxEPD2_154_Z90c(/*CS=*/ 15, /*DC=*/ 27, /*RST=*/ 26, /*BUSY=*/ 25));

// WiFi und MQTT Clients
WiFiClient espClient;
PubSubClient client(espClient);

// Zähler Variable
int counter = 0;

// Funktionsdeklarationen
void drawHeartWithNumber();
void setupWiFi();

// WiFi verbinden
void setupWiFi() {
    Serial.print("Verbinde mit WiFi...");
    WiFi.begin(ssid, password);
    
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    
    Serial.println("");
    Serial.println("WiFi verbunden!");
    Serial.print("IP Adresse: ");
    Serial.println(WiFi.localIP());
}

// MQTT Callback Funktion
void callback(char* topic, byte* payload, unsigned int length) {
    String message = "";
    for (int i = 0; i < length; i++) {
        message += (char)payload[i];
    }
    
    Serial.print("Nachricht empfangen: ");
    Serial.println(message);
    
    // Eigene Zahl erhöhen (nicht die empfangene Zahl übernehmen)
    counter++;
    
    // Display mit neuer Zahl aktualisieren
    drawHeartWithNumber();
}

// MQTT verbinden
void reconnect() {
    while (!client.connected()) {
        Serial.print("Verbinde mit MQTT...");
        String clientId = "ESP32Heart-";
        clientId += String(random(0xffff), HEX);
        
        if (client.connect(clientId.c_str(), mqtt_username, mqtt_password)) {
            Serial.println("verbunden!");
            client.subscribe(mqtt_topic);
        } else {
            Serial.print("fehlgeschlagen, rc=");
            Serial.print(client.state());
            Serial.println(" versuche es in 5 Sekunden erneut...");
            delay(5000);
        }
    }
}

// Herz mit Zahl zeichnen
void drawHeartWithNumber() {
    Serial.println("Zeichne rotes Herz mit Zahl...");
    
    display.setFullWindow();
    display.firstPage();
    do {
        // Weißer Hintergrund
        display.fillScreen(GxEPD_WHITE);
        
        // Herz-Parameter für 200x200 Display (größer)
        int centerX = 100;  // Mitte X (200/2)
        int centerY = 65;   // 5 Pixel nach unten verschoben
        int heartSize = 70; // Größeres Herz
        
        // Obere Hälfte: Zwei Kreise näher zusammen und größer
        int circleRadius = heartSize/2 + 4;  // 4 Pixel größer für schönere Kreise
        int circleY = centerY - heartSize/3;
        int circleSpacing = heartSize/2 - 3;  // 3 Pixel näher zusammen
        
        // Linker oberer Kreis
        display.fillCircle(centerX - circleSpacing, circleY, circleRadius, GxEPD_RED);
        
        // Rechter oberer Kreis  
        display.fillCircle(centerX + circleSpacing, circleY, circleRadius, GxEPD_RED);
        
        // Untere Hälfte: Dreieck für das kleinere Herz
        int triangleTop = centerY - 2;  // 2 Pixel höher für bessere Harmonie
        int triangleBottom = centerY + heartSize + 20;  // Kleiner nach unten für kleineres Herz
        
        // Zeichne das Dreieck Zeile für Zeile
        for (int y = triangleTop; y <= triangleBottom; y++) {
            int progress = y - triangleTop;
            int maxWidth = heartSize + 61;  // Noch 2 Pixel schmaler für bessere Proportionen
            int currentWidth = maxWidth - (progress * maxWidth / (triangleBottom - triangleTop));
            
            int leftX = centerX - currentWidth/2;
            int rightX = centerX + currentWidth/2;
            
            if (y < 200 && leftX >= 0 && rightX < 200) {
                display.drawLine(leftX, y, rightX, y, GxEPD_RED);
            }
        }
        
        // Fülle die Lücken zwischen den Kreisen - größer
        display.fillRect(centerX - heartSize/3, circleY - heartSize/6, heartSize*2/3, heartSize/2, GxEPD_RED);
        
        // Zahl unter dem Herz anzeigen (in den unteren 200x20 Pixeln)
        String numberStr = String(counter);
        int16_t x1, y1;
        uint16_t w, h;
        display.getTextBounds(numberStr, 0, 0, &x1, &y1, &w, &h);
        
        // Zahl genau unter der Dreieckspitze positionieren
        int textX = centerX - w/2 - 6;  // 6 Pixel nach links für perfekte Zentrierung
        int textY = 165;  // Noch weiter nach oben für bessere Sichtbarkeit
        
        // Schwarze Zahl auf weißem Hintergrund (größer)
        display.setTextColor(GxEPD_BLACK);
        display.setTextSize(4);  // Größere Zahl
        display.setCursor(textX, textY);
        display.print(numberStr);
        
    } while (display.nextPage());
    
    Serial.println("Rotes Herz mit Zahl gezeichnet!");
}

void setup() {
    Serial.begin(115200);
    Serial.println("=== ESP32 Rotes Herz Display mit MQTT ===");
    
    // SPI auf die Waveshare ESP32 Driver Board Pins setzen
    SPI.begin(/*SCK=*/ 13, /*MISO=*/ 12, /*MOSI=*/ 14, /*SS=*/ 15);
    
    // Display initialisieren
    display.init(115200, true, 2, false);
    
    Serial.println("Display initialisiert...");
    
    // WiFi verbinden
    setupWiFi();
    
    // MQTT Server konfigurieren
    client.setServer(mqtt_server, mqtt_port);
    client.setCallback(callback);
    
    // Erstes Herz mit Zahl zeichnen
    drawHeartWithNumber();
    
    Serial.println("Setup abgeschlossen");
}

void loop() {
    // MQTT Verbindung prüfen und wiederherstellen
    if (!client.connected()) {
        reconnect();
    }
    client.loop();
    
    delay(100);
}
