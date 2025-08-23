
#include <GxEPD2_BW.h>
#include <GxEPD2_3C.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <SPI.h>

// WiFi Konfiguration
const char* ssid = "REDACTED_SSID";
const char* password = "***REMOVED***";

// MQTT Konfiguration (HiveMQ Cloud mit TLS)
const char* mqtt_server = "***REMOVED***";
const int mqtt_port = 8883;  // TLS/SSL Port
const char* mqtt_username = "***REMOVED***";
const char* mqtt_password = "***REMOVED***";
const char* mqtt_topic = "esp32/heart_counter";

// Waveshare ESP32 Driver Board Pins
GxEPD2_3C<GxEPD2_154_Z90c, GxEPD2_154_Z90c::HEIGHT> display(GxEPD2_154_Z90c(/*CS=*/ 15, /*DC=*/ 27, /*RST=*/ 26, /*BUSY=*/ 25));

// WiFi und MQTT Clients
WiFiClientSecure espClient;  // SSL-Client für HiveMQ Cloud
PubSubClient client(espClient);

// Zähler Variable
int counter = 0;

            // Button-Logik Variablen
            // Button und LED Variablen
            const int BUTTON_PIN = 2;
            const int BUTTON_LED_PIN = 4;
            int lastButtonState = LOW;
            unsigned long lastDebounceTime = 0;
            const unsigned long debounceDelay = 1;   // Nur 1ms für sehr kurze Pulse
            unsigned long ledOnTime = 0;        // Zeitpunkt wann LED eingeschaltet wurde
            unsigned long ledDuration = 0;      // Dauer wie lange LED leuchten soll
            bool ledActive = false;             // LED ist aktiv
            
            // Debug-Variablen
            unsigned long lastDebugTime = 0;
            int debugCounter = 0;

// Funktionsdeklarationen
void drawHeartWithNumber();
void setupWiFi();
void setButtonLEDForDuration(unsigned long duration_ms);
void checkLEDStatus();
void handleButtonPress();

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
    Serial.print("Gateway: ");
    Serial.println(WiFi.gatewayIP());
    Serial.print("DNS: ");
    Serial.println(WiFi.dnsIP());
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
        Serial.print("Verbinde mit MQTT (HiveMQ Cloud TLS)...");
        String clientId = "ESP32Heart-";
        clientId += String(random(0xffff), HEX);
        
        Serial.print("Client ID: ");
        Serial.println(clientId);
        Serial.print("Server: ");
        Serial.print(mqtt_server);
        Serial.print(":");
        Serial.println(mqtt_port);

        // WiFi-Status vor DNS-Test prüfen
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("WiFi nicht verbunden! Versuche Wiederherstellung...");
            WiFi.reconnect();
            delay(5000);
            continue;
        }

        // DNS Test
        IPAddress serverIP;
        if (WiFi.hostByName(mqtt_server, serverIP)) {
            Serial.print("DNS erfolgreich aufgelöst: ");
            Serial.println(serverIP);
        } else {
            Serial.println("DNS Auflösung fehlgeschlagen!");
            Serial.println("WiFi-Status prüfen und neu verbinden...");
            WiFi.reconnect();
            delay(10000);
            continue;
        }

        if (client.connect(clientId.c_str(), mqtt_username, mqtt_password)) {
            Serial.println("MQTT verbunden!");
            Serial.print("Subscribing zu Topic: ");
            Serial.println(mqtt_topic);
            client.subscribe(mqtt_topic);
        } else {
            Serial.print("MQTT fehlgeschlagen, rc=");
            Serial.print(client.state());
            Serial.println(" (0=Connection timeout, -1=Connection lost, -2=Connect failed, -3=Connection lost, -4=Connection failed, -5=Connection timeout)");
            Serial.println("Versuche es in 5 Sekunden erneut...");
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

// LED für bestimmte Zeit einschalten
void setButtonLEDForDuration(unsigned long duration_ms) {
    Serial.println("=== LED EIN ===");
    Serial.print("LED Pin (GPIO ");
    Serial.print(BUTTON_LED_PIN);
    Serial.println(") wird auf HIGH gesetzt...");
    
    ledOnTime = millis();
    ledDuration = duration_ms;
    ledActive = true;
    
    digitalWrite(BUTTON_LED_PIN, HIGH);
    Serial.println("LED ist jetzt an!");
    Serial.println("===============");
}

// LED für kurze Zeit blinken lassen (2x)
void blinkLEDTwice() {
    Serial.println("💡 LED blinkt 2x...");
    
    // 1. Blink
    digitalWrite(BUTTON_LED_PIN, HIGH);
    delay(100);
    digitalWrite(BUTTON_LED_PIN, LOW);
    delay(100);
    
    // 2. Blink
    digitalWrite(BUTTON_LED_PIN, HIGH);
    delay(100);
    digitalWrite(BUTTON_LED_PIN, LOW);
    
    Serial.println("💡 LED 2x geblinkt!");
}

// LED Status prüfen und ausschalten wenn Zeit abgelaufen
void checkLEDStatus() {
    if (ledActive) {
        unsigned long elapsed = millis() - ledOnTime;
        Serial.print("LED läuft seit: ");
        Serial.print(elapsed);
        Serial.print("ms von ");
        Serial.print(ledDuration);
        Serial.println("ms");

        if (elapsed >= ledDuration) {
            Serial.println("LED Zeit abgelaufen - wird ausgeschaltet!");
            digitalWrite(BUTTON_LED_PIN, LOW);
            ledActive = false;
        }
    }
}

// Debug-Funktion - Status alle 5 Sekunden ausgeben
void debugStatus() {
    if (millis() - lastDebugTime > 5000) {  // Alle 5 Sekunden
        lastDebugTime = millis();
        debugCounter++;
        
        Serial.println("=== DEBUG STATUS ===");
        Serial.print("Debug Counter: ");
        Serial.println(debugCounter);
        Serial.print("Button State: ");
        Serial.println(digitalRead(BUTTON_PIN));
        Serial.print("LED State: ");
        Serial.println(digitalRead(BUTTON_LED_PIN));
        Serial.print("ledActive: ");
        Serial.println(ledActive);
        Serial.print("ledDuration: ");
        Serial.println(ledDuration);
        Serial.print("ledOnTime: ");
        Serial.println(ledOnTime);
        Serial.print("millis(): ");
        Serial.println(millis());
        Serial.println("==================");
    }
}

// Button-Event verarbeiten
void handleButtonPress() {
  Serial.println("🎯 Button-Druck erkannt!");
  Serial.println("Sende MQTT-Nachricht an andere ESP32...");
  
  // LED 2x blinken für Button-Bestätigung
  blinkLEDTwice();
  
  // MQTT Nachricht senden (ohne lokalen Counter zu erhöhen)
  if (client.connected()) {
    // Sende den aktuellen Counter-Wert (nicht erhöht)
    String message = String(counter);
    if (client.publish("esp32/heart_counter", message.c_str())) {
      Serial.println("✅ MQTT Nachricht erfolgreich gesendet!");
      Serial.print("Gesendeter Wert: ");
      Serial.println(message);
      // LED nochmal 2x blinken nach erfolgreicher MQTT-Sendung
      delay(500);  // Kurze Pause
      blinkLEDTwice();
    } else {
      Serial.println("❌ MQTT Sendung fehlgeschlagen!");
    }
  } else {
    Serial.println("❌ MQTT nicht verbunden!");
  }
  
  // KEIN Display-Update - das soll nur der andere ESP32 machen!
  Serial.println("Display bleibt unverändert - Counter wird nur per MQTT gesendet!");
}



void setup() {
    Serial.begin(115200);
    Serial.println("=== ESP32 Rotes Herz Display mit MQTT ===");
    
                    // Button-Pin konfigurieren (Push-On Button)
                pinMode(BUTTON_PIN, INPUT);  // GPIO 2 für Push-On Button (kein Pull-up nötig)
                pinMode(BUTTON_LED_PIN, OUTPUT);  // Button-LED Pin
    
    // SPI auf die Waveshare ESP32 Driver Board Pins setzen
    SPI.begin(/*SCK=*/ 13, /*MISO=*/ 12, /*MOSI=*/ 14, /*SS=*/ 15);
    
    // Display initialisieren
    display.init(115200, true, 2, false);
    
    Serial.println("Display initialisiert...");
    
                    // WiFi verbinden
                setupWiFi();

                // SSL für HiveMQ Cloud konfigurieren
                espClient.setInsecure();  // Für Test - in Produktion sollte man Zertifikate verwenden
                
                // MQTT Server konfigurieren
                client.setServer(mqtt_server, mqtt_port);
                client.setCallback(callback);
    
    Serial.println("Setup abgeschlossen");
    
    // Erstes Herz mit Zahl zeichnen
    drawHeartWithNumber();
    
                    // Start-Blinken der Button-LED
                for (int i = 0; i < 3; i++) {
                    digitalWrite(BUTTON_LED_PIN, HIGH);
                    delay(200);
                    digitalWrite(BUTTON_LED_PIN, LOW);
                    delay(200);
                }
                
                // LED nach dem Start-Blinken ausschalten
                digitalWrite(BUTTON_LED_PIN, LOW);
                Serial.println("LED ist jetzt aus!");
}

void loop() {
  // Button lesen - SOFORTIGE Reaktion ohne Debouncing
  int reading = digitalRead(BUTTON_PIN);
  
  // Wenn sich der Button-State geändert hat
  if (reading != lastButtonState) {
    Serial.print("Button State geändert: ");
    Serial.print(lastButtonState);
    Serial.print(" -> ");
    Serial.println(reading);
    
    // SOFORT Button-Druck erkennen (HIGH) wenn vorher LOW war
    if (reading == HIGH && lastButtonState == LOW) {
      handleButtonPress();
    }
  }
  
  lastButtonState = reading;
  
  // LED Status prüfen
  checkLEDStatus();
  
  // MQTT Client Loop
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  
  // WiFi Status prüfen
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi verloren! Versuche Reconnect...");
    WiFi.reconnect();
  }
  
  // Debug Status alle 5 Sekunden
  static unsigned long lastDebugTime = 0;
  if (millis() - lastDebugTime > 5000) {
    debugStatus();
    lastDebugTime = millis();
  }
  
  // Button Debug alle 2 Sekunden
  static unsigned long lastButtonDebug = 0;
  if (millis() - lastButtonDebug > 2000) {
    Serial.print("Button Debug - Reading: ");
    Serial.print(reading);
    Serial.print(", Last State: ");
    Serial.print(lastButtonState);
    Serial.print(", Debounce Time: ");
    Serial.println(millis() - lastDebounceTime);
    lastButtonDebug = millis();
  }
  
  // Einfacher Button-Test jede Sekunde
  static unsigned long lastSimpleTest = 0;
  if (millis() - lastSimpleTest > 1000) {
    int buttonValue = digitalRead(BUTTON_PIN);
    Serial.print("🔧 GPIO 2 Wert: ");
    Serial.print(buttonValue);
    if (buttonValue == HIGH) {
      Serial.println(" ✅ BUTTON GEDRÜCKT!");
    } else {
      Serial.println(" ❌ Button nicht gedrückt");
    }
    lastSimpleTest = millis();
  }
  
  delay(5);  // Reduziert von 10ms auf 5ms für schnellere Reaktion
}
