#include <Wire.h>
#include <Adafruit_PN532.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include <ArduinoJson.h>      // Required for parsing JSON from Google Sheets
// #include <strptime_compat.h>  // <--- REMOVE THIS LINE OR COMMENT IT OUT


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
const char* ssid = "POCO F5";         // Replace with your WiFi SSID
const char* password = "andibudicandra"; // Replace with your WiFi password

// ===== GOOGLE SHEETS URLs =====
const char* GOOGLE_SHEETS_LOG_URL = "https://script.google.com/macros/s/AKfycbzSZmMZB0BDx5tJfCbx0dliWau2sesMtBVoz6abQPZZRhyMfC_3M2zi5u81_q3Dj3YM/exec"; // Your existing log URL
const char* GOOGLE_SHEETS_FETCH_URL = "https://script.google.com/macros/s/AKfycbySrBC2Vnx34-CxWNHNWfPVND0O5KRpc3Tmf989hNB8wfZWsyZrAjN4JAJ0zd6AisQy/exec"; // <--- IMPORTANT: Paste your new Web app URL here

// --- REMOVE OR COMMENT OUT THIS LINE ---
// const int ON_TIME_GRACE_PERIOD_SECONDS = 5 * 60; // No longer needed for this new logic

const int TIME_OFFSET_SECONDS = 7 * 3600; // Offset for WIB (Western Indonesia Time) = UTC+7 hours. Adjust if your timezone is different.


int failCount = 0;
const int maxFailCount = 3;

// ===== STRUCTURE FOR CARD DATA =====
struct CardData {
  String status;       // "ON_TIME", "LATE", "INVALID"
  String targetTime;   // The target time string fetched from the sheet
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
    if (isalnum(c)) {
      encoded += c;
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

// Function to log data to your existing Google Sheet (using value1, value2, value3 for your script)
void logToGoogleSheets(String uidStr, String status) {
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClientSecure client;
    client.setInsecure();  // Needed for Google Apps Script
    HTTPClient http;

    String fullURL = String(GOOGLE_SHEETS_LOG_URL) +
                     "?value1=" + urlEncode(uidStr) +
                     "&value2=" + urlEncode(status) +
                     "&value3=" + urlEncode(getFormattedTime(time(NULL))); // Log current scan time

    Serial.print("Logging to: ");
    Serial.println(fullURL);

    http.begin(client, fullURL);
    int httpResponseCode = http.GET(); // Your logging script uses GET

    Serial.print("Google Sheets log response: ");
    Serial.println(httpResponseCode);

    if (httpResponseCode > 0) {
      String payload = http.getString();
      Serial.println(payload);
    }
    http.end();
  } else {
    Serial.println("WiFi not connected for logging.");
  }
}

// New function to fetch card data (UID and Target Time) from the Google Apps Script
CardData fetchCardData(String uidStr) {
  CardData data;
  data.status = "INVALID"; // Default to INVALID

  if (WiFi.status() == WL_CONNECTED) {
    WiFiClientSecure client;
    client.setInsecure(); // Needed for Google Apps Script
    HTTPClient http;

    String fullURL = String(GOOGLE_SHEETS_FETCH_URL) + "?uid=" + urlEncode(uidStr);

    Serial.print("Fetching from: ");
    Serial.println(fullURL);

    http.begin(client, fullURL);
    int httpResponseCode = http.GET();

    if (httpResponseCode > 0) {
      String payload = http.getString();
      Serial.print("Google Sheets fetch response: ");
      Serial.println(httpResponseCode);
      Serial.println("Payload: " + payload);

      StaticJsonDocument<200> doc; // Buffer size for JSON, adjust if your JSON is larger
      DeserializationError error = deserializeJson(doc, payload);

      if (error) {
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.f_str());
      } else {
        const char* fetchedStatus = doc["status"]; // Check for direct status if UID not found
        const char* fetchedTargetTime = doc["targetTime"];

        if (fetchedStatus && String(fetchedStatus) == "INVALID_UID") {
          data.status = "INVALID"; // UID not found in sheet
          data.targetTime = "";
        } else if (fetchedTargetTime) {
          data.targetTime = String(fetchedTargetTime);
          Serial.print("Fetched Target Time: ");
          Serial.println(data.targetTime);
          // Status will be determined later by time comparison
          data.status = "VALID_PENDING_TIME_CHECK"; 
        } else {
          Serial.println("Unexpected JSON payload (missing targetTime or status).");
          data.status = "INVALID"; // Unexpected response
        }
      }
    } else {
      Serial.print("Error on HTTP request to fetch: ");
      Serial.println(httpResponseCode);
      data.status = "INVALID"; // Network error
    }
    http.end();
  } else {
    Serial.println("WiFi not connected for fetching card data.");
    data.status = "INVALID"; // No WiFi
  }
  return data;
}

// Custom helper to parse date string into time_t (YYYY-MM-DD HH:MM:SS)
time_t parseDateTime(String dateTimeStr) {
  struct tm tm_struct;
  memset(&tm_struct, 0, sizeof(tm_struct)); // Initialize to all zeros

  // Parse YYYY
  tm_struct.tm_year = dateTimeStr.substring(0, 4).toInt() - 1900;
  // Parse MM
  tm_struct.tm_mon = dateTimeStr.substring(5, 7).toInt() - 1;
  // Parse DD
  tm_struct.tm_mday = dateTimeStr.substring(8, 10).toInt();
  // Parse HH
  tm_struct.tm_hour = dateTimeStr.substring(11, 13).toInt();
  // Parse MM
  tm_struct.tm_min = dateTimeStr.substring(14, 16).toInt();
  // Parse SS
  tm_struct.tm_sec = dateTimeStr.substring(17, 19).toInt();

  // Validate parsed values (basic check)
  if (tm_struct.tm_year < 0 || tm_struct.tm_mon < 0 || tm_struct.tm_mon > 11 ||
      tm_struct.tm_mday < 1 || tm_struct.tm_mday > 31 ||
      tm_struct.tm_hour < 0 || tm_struct.tm_hour > 23 ||
      tm_struct.tm_min < 0 || tm_struct.tm_min > 59 ||
      tm_struct.tm_sec < 0 || tm_struct.tm_sec > 59) {
    Serial.println("Failed to parse date/time string: " + dateTimeStr + " - Invalid values.");
    return 0; // Return 0 for invalid time
  }

  return mktime(&tm_struct); // Convert tm_struct to time_t
}


void connectToWiFi() {
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    lcd.print(".");
  }
  Serial.println("\nConnected!");
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WiFi Connected!");
  lcd.setCursor(0, 1);
  lcd.print(WiFi.localIP());
  Serial.println(WiFi.localIP());

  // Configure NTP for time synchronization
  // Timezone for WIB (Western Indonesia Time): UTC+7. So, gmtOffset_sec = 7 * 3600
  // daylightOffset_sec is 0 for regions without DST.
  configTime(TIME_OFFSET_SECONDS, 0, "pool.ntp.org", "time.nist.gov");
  Serial.println("Waiting for NTP time sync...");
  time_t now = time(NULL);
  int retryCount = 0;
  while (now < 8 * 3600 * 2 && retryCount < 10) { // Check if time is somewhat valid (after Jan 1, 1970)
    delay(500);
    Serial.print(".");
    now = time(NULL);
    retryCount++;
  }
  Serial.println("\nTime synchronized. Current local time: " + getFormattedTime(now));
  delay(1000); // Give a moment for time to settle
}

void resetNFC() {
  Wire.end();
  delay(10);
  Wire.begin(SDA_PIN, SCL_PIN, 100000); // Re-initialize I2C for NFC
  nfc.begin();
  nfc.SAMConfig();
  Serial.println("PN532 reset complete.");
}

// ===== SETUP =====
void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN, 100000); // Initialize I2C for NFC and LCD

  pinMode(RED_LED, OUTPUT);
  pinMode(YELLOW_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(BUZZER, OUTPUT);
  resetLEDs(); // Ensure all LEDs are off at start

  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Starting System...");
  delay(1000);

  nfc.begin();
  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata) {
    Serial.println("PN532 not found");
    lcd.setCursor(0, 1);
    lcd.print("PN532 not found!");
    while (1) {
      digitalWrite(RED_LED, HIGH);
      delay(500);
      digitalWrite(RED_LED, LOW);
      delay(500);
    } // Halt and blink red LED if NFC is not found
  }
  nfc.SAMConfig(); // Configure NFC for normal operation

  connectToWiFi(); // Connect to WiFi and sync time

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Ready to scan...");
  Serial.println("Ready to scan...");
}

// ===== LOOP =====
void loop() {
  uint8_t uid[7];
  uint8_t uidLength;
  bool success = false;

  // Try reading NFC three times with a small delay
  for (int i = 0; i < 3 && !success; i++) {
    success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 200);
    if (!success) delay(100);
  }

  if (success) {
    failCount = 0; // Reset fail count on successful read

    String uidStr = "";
    for (int i = 0; i < uidLength; i++) {
      if (uid[i] < 0x10) uidStr += "0";
      uidStr += String(uid[i], HEX);
      if (i < uidLength - 1) uidStr += " ";
    }
    uidStr.toUpperCase(); // Ensure UID is uppercase for consistent comparison

    Serial.print("Scanned UID: ");
    Serial.println(uidStr);

    digitalWrite(BUZZER, HIGH); // Buzzer sound for successful scan
    delay(100);
    digitalWrite(BUZZER, LOW);
    resetLEDs(); // Turn off all LEDs before showing new status

    // Get card data (including target time) from Google Sheets
    CardData card = fetchCardData(uidStr);
    String finalStatus = "INVALID"; // Default status

    if (card.status == "VALID_PENDING_TIME_CHECK") {
      time_t currentTime = time(NULL); // Get current timestamp
      time_t targetTimestamp = parseDateTime(card.targetTime); // Parse target time from sheet

      if (targetTimestamp != 0) { // Check if target time was successfully parsed
        // Calculate difference: positive if current time is after target, negative if before
        long diffSeconds = currentTime - targetTimestamp; 

        if (diffSeconds <= 0) { // Current time is before or exactly at target time
          finalStatus = "ON_TIME";
        } else { // Current time is after target time (even by 1 second)
          finalStatus = "LATE";
        }
      } else {
        Serial.println("Invalid target time parsed from sheet for UID: " + uidStr);
        finalStatus = "INVALID_TARGET_TIME_PARSE_ERROR"; // Indicate parsing error
      }
    } else {
      finalStatus = card.status; // Directly use status from fetch (e.g., "INVALID_UID", "ERROR_...")
    }

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("UID: " + uidStr);
    lcd.setCursor(0, 1);

    // Control LEDs and LCD based on the determined status
    if (finalStatus == "ON_TIME") {
      digitalWrite(GREEN_LED, HIGH);
      lcd.print("Status: On Time");
    } else if (finalStatus == "LATE") {
      digitalWrite(YELLOW_LED, HIGH);
      lcd.print("Status: Late");
    } else { // Covers "INVALID", "INVALID_UID", "INVALID_TARGET_TIME_PARSE_ERROR", etc.
      digitalWrite(RED_LED, HIGH);
      lcd.print("Status: Invalid"); // Simplified display for all "invalid" states
      digitalWrite(BUZZER, HIGH); // Additional buzzer for invalid/unidentified
      delay(500);
      digitalWrite(BUZZER, LOW);
    }
    
    lcd.setCursor(0, 2);
    lcd.print(getFormattedTime(time(NULL))); // Display current scan time

    // Log the UID and its determined status to your original logging sheet
    logToGoogleSheets(uidStr, finalStatus); 

    delay(3000); // Display status for 3 seconds
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Ready to scan...");
    resetLEDs(); // Turn off LEDs after displaying status
    delay(200);

  } else {
    failCount++;
    Serial.println("NFC read failed.");

    if (failCount >= maxFailCount) {
      Serial.println("Too many failures. Resetting PN532...");
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Resetting PN532...");
      resetNFC(); // Attempt to reset NFC module
      failCount = 0; // Reset fail count after attempting reset
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Ready to scan...");
    }

    delay(200); // Small delay before next loop iteration
  }
}