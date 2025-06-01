#include <Wire.h>
#include <Adafruit_PN532.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include <ArduinoJson.h>

// ===== PIN DEFINITIONS =====
#define SDA_PIN 21
#define SCL_PIN 22
#define RED_LED 16
#define YELLOW_LED 17
#define GREEN_LED 18
#define BUZZER 19

// ===== NFC SETUP =====
Adafruit_PN532 nfc(-1, -1, &Wire);

// ===== LCD SETUP =====
LiquidCrystal_I2C lcd(0x27, 20, 4);

// ===== WIFI SETUP =====
const char* ssid = "YOURWIFINAME"; // EDIT BEFORE USE
const char* password = "YOURWIFIPASS";

// ===== GOOGLE SHEETS URL (Unified Script) =====
const char* GOOGLE_SHEETS_UNIFIED_URL = "https://script.google.com/macros/s/AKfycbzQm1n0TXx7P0oZokOIcK6Mgbl9rpQa-QiMsPabPd2lLcgdmjVdvERxmr2VEaN8lNhSaQ/exec"; // PASTE URL FROM DEPLOYED SCRIPT

const int TIME_OFFSET_SECONDS = 7 * 3600; // WIB = UTC+7

int failCount = 0;
const int maxFailCount = 3;

// ===== STRUCTURE FOR PROCESSED CARD DATA =====
struct ProcessedCardData {
  String script_status; // Detailed status from script's internal processing
  String attendance_status_from_script; // ON_TIME, LATE, INVALID_UID, etc. from script
  String target_time_str;
  bool logged;
  String error_message_from_script;
};

// ===== FUNCTIONS =====
void resetLEDs() {
  digitalWrite(RED_LED, LOW);
  digitalWrite(YELLOW_LED, LOW);
  digitalWrite(GREEN_LED, LOW);
}

String getFormattedTime(time_t rawtime) {
  struct tm *timeinfo;
  timeinfo = localtime(&rawtime);
  char buf[64];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", timeinfo);
  return String(buf);
}

String urlEncode(String str) {
  String encoded = "";
  char c;
  char code0, code1;
  for (int i = 0; i < str.length(); i++) {
    c = str.charAt(i);
    if (isalnum(c) || c == ' ' || c == ':' || c == '-') {
      if(c == ' ') encoded += "%20";
      else if (c == ':') encoded += "%3A";
      else encoded += c;
    } else {
      code0 = (c >> 4) & 0xF;
      code1 = c & 0xF;
      encoded += '%';
      encoded += "0123456789ABCDEF"[code0];
      encoded += "0123456789ABCDEF"[code1];
    }
  }
  return encoded;
}

// No longer needed: time_t parseDateTime(String dateTimeStr) {...} as script handles time comparison

ProcessedCardData processAndLogCardScan(String uidStr) {
  ProcessedCardData pcd;
  pcd.script_status = "ESP_INIT_FAILURE";
  pcd.attendance_status_from_script = "INVALID_ESP_INIT";
  pcd.logged = false;

  if (WiFi.status() == WL_CONNECTED) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;

    String scanTimeStr = getFormattedTime(time(NULL));
    String fullURL = String(GOOGLE_SHEETS_UNIFIED_URL) +
                     "?uid=" + urlEncode(uidStr) +
                     "&scanTime=" + urlEncode(scanTimeStr);

    Serial.println("-----------------------------------");
    Serial.print("Unified Script Call: "); Serial.println(fullURL);

    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    http.begin(client, fullURL);
    int httpResponseCode = http.GET();
    String payload = "";

    if (httpResponseCode > 0) {
        payload = http.getString();
    }

    Serial.print("Unified Script Response Code: "); Serial.println(httpResponseCode);
    Serial.println("Raw Payload: [" + payload + "]");

    if (httpResponseCode == HTTP_CODE_OK) {
      StaticJsonDocument<512> doc; 
      DeserializationError error = deserializeJson(doc, payload);

      if (error) {
        Serial.print(F("deserializeJson() failed: ")); Serial.println(error.f_str());
        pcd.script_status = "ESP_JSON_PARSE_ERROR";
        pcd.attendance_status_from_script = "INVALID_JSON_ERROR";
        pcd.error_message_from_script = error.c_str();
      } else {
        Serial.println("deserializeJson() success.");
        pcd.script_status = doc["script_status"].as<String>();
        pcd.attendance_status_from_script = doc["attendance_status"].as<String>(); // Get status from script
        pcd.target_time_str = doc["target_time"].as<String>(); 
        pcd.logged = doc["logged"].as<bool>();
        pcd.error_message_from_script = doc["error_message"].as<String>();

        Serial.print("Script Processing Status Reported: "); Serial.println(pcd.script_status);
        Serial.print("Attendance Status from Script: "); Serial.println(pcd.attendance_status_from_script);
        Serial.print("Logged by Script: "); Serial.println(pcd.logged ? "Yes" : "No");

        if (pcd.target_time_str.length() > 0 && pcd.target_time_str != "null") {
            Serial.print("Target Time from Script: "); Serial.println(pcd.target_time_str);
        }
        if (pcd.error_message_from_script.length() > 0 && pcd.error_message_from_script != "null") {
            Serial.print("Error Message from Script: "); Serial.println(pcd.error_message_from_script);
        }
        // ESP32 now directly uses attendance_status_from_script
      }
    } else {
      Serial.print("Error on HTTP request: "); Serial.println(http.errorToString(httpResponseCode).c_str());
      pcd.script_status = "ESP_HTTP_ERROR";
      pcd.attendance_status_from_script = "INVALID_HTTP_ERROR";
      pcd.error_message_from_script = http.errorToString(httpResponseCode).c_str();
    }
    http.end();
  } else {
    Serial.println("WiFi not connected for unified call.");
    pcd.script_status = "ESP_NO_WIFI";
    pcd.attendance_status_from_script = "INVALID_NO_WIFI";
  }
  Serial.print("processAndLogCardScan returning attendance_status: "); Serial.println(pcd.attendance_status_from_script);
  Serial.println("-----------------------------------");
  return pcd;
}

void connectToWiFi() {
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  lcd.clear(); lcd.setCursor(0, 0); lcd.print("Connecting WiFi");
  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 20) {
    delay(500); Serial.print("."); lcd.print("."); retries++;
  }
  if(WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected!");
    lcd.clear(); lcd.setCursor(0, 0); lcd.print("WiFi Connected!");
    lcd.setCursor(0, 1); lcd.print(WiFi.localIP());
    Serial.println(WiFi.localIP());

    configTime(TIME_OFFSET_SECONDS, 0, "pool.ntp.org", "time.nist.gov");
    Serial.println("Waiting for NTP time sync...");
    lcd.setCursor(0,2); lcd.print("Syncing time...");
    time_t now = time(NULL); int ntpRetryCount = 0;
    while (now < 8 * 3600 * 2 && ntpRetryCount < 20) {
      delay(500); Serial.print("."); now = time(NULL); ntpRetryCount++;
    }
    if (now < 8 * 3600 * 2) {
        Serial.println("\nNTP Sync Failed!");
        lcd.setCursor(0,2); lcd.print("NTP Sync Failed!   ");
    } else {
        Serial.println("\nTime synchronized.");
        lcd.setCursor(0,2); lcd.print("Time Synced        ");
    }
  } else {
    Serial.println("\nWiFi Connection Failed!");
    lcd.clear(); lcd.setCursor(0,0); lcd.print("WiFi Failed!");
  }
  delay(1000);
}

void resetNFC() {
  Serial.println("Attempting PN532 reset...");
  Wire.end(); delay(100); Wire.begin(SDA_PIN, SCL_PIN, 100000); delay(100);
  nfc.begin();
  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata) { Serial.println("PN532 still not found after reset."); }
  else {
    Serial.println("PN532 re-initialized. FW Ver: 0x" + String(versiondata, HEX));
    nfc.SAMConfig(); Serial.println("PN532 SAMConfig complete after reset.");
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN, 100000);

  pinMode(RED_LED, OUTPUT); pinMode(YELLOW_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT); pinMode(BUZZER, OUTPUT);
  resetLEDs();

  lcd.init(); lcd.backlight(); lcd.clear();
  lcd.setCursor(0, 0); lcd.print("System Starting...");
  delay(1000);

  nfc.begin();
  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata) {
    Serial.println("PN532 not found!"); lcd.setCursor(0, 1); lcd.print("PN532 NOT FOUND!");
    while (1) { digitalWrite(RED_LED, HIGH); delay(300); digitalWrite(RED_LED, LOW); delay(300); }
  }
  Serial.println("PN532 Found. FW Ver: 0x" + String(versiondata, HEX));
  nfc.SAMConfig(); Serial.println("PN532 SAMConfig complete.");

  connectToWiFi();

  lcd.clear(); lcd.setCursor(0, 0); lcd.print("Ready to scan...");
  Serial.println("Ready to scan...");
}

// ===== LOOP =====
void loop() {
  uint8_t uid[7];
  uint8_t uidLength;
  bool successNFCRead = false;

  successNFCRead = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 50);

  if (successNFCRead) {
    failCount = 0;
    String uidStr = "";
    for (int i = 0; i < uidLength; i++) {
      if (uid[i] < 0x10) uidStr += "0";
      uidStr += String(uid[i], HEX);
      if (i < uidLength - 1) uidStr += " ";
    }
    uidStr.toUpperCase();

    Serial.print("\nScanned UID: "); Serial.println(uidStr);

    digitalWrite(BUZZER, HIGH); delay(50); digitalWrite(BUZZER, LOW);
    resetLEDs();

    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("Card Scanned:");
    lcd.setCursor(0, 1); lcd.print(uidStr.substring(0,11)); 
    lcd.setCursor(0, 2); lcd.print("Uploading data...");
    
    ProcessedCardData cardResult = processAndLogCardScan(uidStr);

    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("UID: " + uidStr.substring(0,11));
    lcd.setCursor(0, 1);

    String displayStatus = cardResult.attendance_status_from_script; 

    Serial.print("Final Status (from Script): "); Serial.println(displayStatus);
    Serial.print("Logged by Script: "); Serial.println(cardResult.logged ? "Yes" : "No");

    if (displayStatus == "ON_TIME") {
      digitalWrite(GREEN_LED, HIGH);
      lcd.print("Status: On Time");
    } else if (displayStatus == "LATE") {
      digitalWrite(YELLOW_LED, HIGH);
      lcd.print("Status: Late");
    } else { 
      digitalWrite(RED_LED, HIGH);
      lcd.print("Status: Invalid"); 
      if (displayStatus != "VALID_UID_NO_TARGET_TIME" && displayStatus != "INVALID_UID") { // Don't long buzz for simple UID issues handled by red LED
        digitalWrite(BUZZER, HIGH); delay(200); digitalWrite(BUZZER, LOW);
      }
    }
    
    lcd.setCursor(0, 2); lcd.print(getFormattedTime(time(NULL)));
    lcd.setCursor(0,3);
    
    if (!cardResult.logged && 
        !(displayStatus.startsWith("INVALID_") || displayStatus.startsWith("ERROR_") || cardResult.script_status.startsWith("ESP_"))) {
        lcd.print("Log Failed!");
    } else if (displayStatus == "INVALID_DATE_PARSE_FOR_COMPARE" || displayStatus == "INVALID_TARGET_TIME_FORMAT_FOR_COMPARE" || cardResult.script_status == "ESP_JSON_PARSE_ERROR") {
        lcd.print("Data Error");
    } else if (displayStatus == "INVALID_HTTP_ERROR" || displayStatus == "INVALID_SCRIPT_ERROR" || cardResult.script_status.startsWith("ERROR_") || cardResult.script_status == "SCRIPT_EXECUTION_ERROR" || cardResult.script_status == "ESP_HTTP_ERROR") {
        lcd.print("Comms Error");
    } else if (displayStatus == "VALID_UID_NO_TARGET_TIME") {
        lcd.print("UID Valid,No Time"); // Shortened
    } else if (displayStatus == "INVALID_UID") {
        lcd.print("UID Not Registered");
    } else if (displayStatus == "INVALID_MISSING_PARAMS" || displayStatus == "INVALID_DEFAULT" || displayStatus == "INVALID_ESP_INIT" ) {
        lcd.print("System Error");
    }
     else {
        lcd.print("                "); 
    }

    delay(1500); 
    lcd.clear(); lcd.setCursor(0, 0); lcd.print("Ready to scan...");
    resetLEDs();
    delay(100);

  } else {
    failCount++;
    if (failCount > 200) { failCount = 0; }
    if (failCount >= maxFailCount && (failCount % maxFailCount == 0) ) {
      Serial.println("NFC read failed " + String(failCount) + " times. Attempting PN532 reset...");
      resetNFC();
      lcd.clear(); lcd.setCursor(0, 0); lcd.print("Ready to scan...");
    }
    delay(100);
  }
}
