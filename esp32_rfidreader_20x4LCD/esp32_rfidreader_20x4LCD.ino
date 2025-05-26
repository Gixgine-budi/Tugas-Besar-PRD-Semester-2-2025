#include <Wire.h>
#include <Adafruit_PN532.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>

std::vector<String> validUIDs;
String getNameFromUID(String uid); // Forward declaration

// ===== WiFi =====
const char* ssid = "POCO F5";
const char* password = "andibudicandra";

// ===== Google Script URL =====
const String scriptURL = "https://script.google.com/macros/s/AKfycbyAruSckRrB00MFOLU0rsgrCeyEKzbkgIvnnb_0l48ByN21ISg6M7OCAoE39HaXX8hqeA/exec";

// ===== PN532 Setup =====
#define SDA_PIN 21
#define SCL_PIN 22
Adafruit_PN532 nfc(SDA_PIN, SCL_PIN);

// ===== LCD Setup =====
LiquidCrystal_I2C lcd(0x27, 20, 4); // I2C address, 20x4

// ===== LED & Buzzer Pins =====
#define RED_LED 16
#define YELLOW_LED 17
#define GREEN_LED 18
#define BUZZER 19

// ===== NTP Client =====
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 7 * 3600, 60000); // GMT+7 for Indonesia

// ===== Known UIDs =====
String uidYellow = "D3DE28E1";
String uidGreen  = "DF830870";
String uidRed    = "BCDE4F05";

void setup() {
  Serial.begin(115200);

  // LEDs & Buzzer
  pinMode(RED_LED, OUTPUT);
  pinMode(YELLOW_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(BUZZER, OUTPUT);

  // LCD
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Connecting to Wi-Fi");

  // Wi-Fi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  lcd.setCursor(0, 1);
  lcd.print("Wi-Fi connected!");
  Serial.println("Wi-Fi connected");

  fetchValidUIDs();


  // NTP
  timeClient.begin();
  timeClient.update();

  // NFC
  nfc.begin();
  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata) {
    Serial.println("Didn't find PN532");
    while (1);
  }
  nfc.SAMConfig();
  Serial.println("NFC reader ready.");
  lcd.setCursor(0, 2);
  lcd.print("NFC ready.");
}

void loop() {
  uint8_t success;
  uint8_t uid[7];
  uint8_t uidLength;

  success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength);

  if (success) {
    String scannedUID = "";
    for (uint8_t i = 0; i < uidLength; i++) {
      scannedUID += String(uid[i], HEX);
    }
    scannedUID.toUpperCase();

    Serial.print("Card UID: ");
    Serial.println(scannedUID);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("UID: " + scannedUID);

    // Check UID and light up LED
    bool isValid = false;

    for (String knownUID : validUIDs) {
      if (scannedUID == knownUID) {
        isValid = true;
        break;
      }
    }

    String color = "Unknown";
    if (isValid) {
      // Map colors manually if you like:
      if (scannedUID == "D3DE28E1") {
        digitalWrite(YELLOW_LED, HIGH); color = "Yellow";
      } else if (scannedUID == "DF830870") {
        digitalWrite(GREEN_LED, HIGH); color = "Green";
      } else if (scannedUID == "BCDE4F05") {
        digitalWrite(RED_LED, HIGH); color = "Red";
      } else {
        digitalWrite(GREEN_LED, HIGH);
        color = "Valid";
      }
    } else {
      digitalWrite(RED_LED, HIGH);
      digitalWrite(GREEN_LED, HIGH);
      digitalWrite(YELLOW_LED, HIGH);
    }


    // Buzz
    digitalWrite(BUZZER, HIGH);
    delay(100);
    digitalWrite(BUZZER, LOW);

    // Time
    timeClient.update();
    String timeStamp = timeClient.getFormattedTime();

    // LCD update
    lcd.setCursor(0, 1);
    lcd.print("Color: " + color);
    lcd.setCursor(0, 2);
    lcd.print("Time: " + timeStamp);

    // Send to Google Sheets
    sendToSheet(scannedUID, isValid);

    delay(3000); // Wait to avoid reading the same card again
    lcd.clear();
  }
}

void sendToSheet(String uid, bool isValid) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String fullURL = scriptURL + "?uid=" + uid + "&valid=" + (isValid ? "true" : "false");
    http.begin(fullURL);
    int httpCode = http.GET();

    if (httpCode > 0) {
      Serial.println("Sent to Google Sheet.");
    } else {
      Serial.print("Error sending: ");
      Serial.println(httpCode);
    }
    http.end();
  }
}


void fetchValidUIDs() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin("https://script.google.com/macros/s/AKfycbzxLjfSOpNDpW8JNFtyH5PaDZmIeuWTHa_OS_mtDCtQdSgoK32SUGyABn8tBmGq1qpq/exec");  // Replace with your link
    int httpCode = http.GET();

    if (httpCode == 200) {
      String payload = http.getString();
      Serial.println("UID List received:");
      Serial.println(payload);

      DynamicJsonDocument doc(2048);
      deserializeJson(doc, payload);

      validUIDs.clear();
      for (JsonObject obj : doc.as<JsonArray>()) {
        validUIDs.push_back(obj["uid"].as<String>());
      }
    } else {
      Serial.println("Failed to fetch UID list.");
    }

    http.end();
  }
}