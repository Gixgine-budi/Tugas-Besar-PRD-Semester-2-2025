#include <Wire.h>
#include <Adafruit_PN532.h>
#include <LiquidCrystal_I2C.h>

// I2C setup (shared for LCD and PN532)
#define SDA_PIN 21
#define SCL_PIN 22

// LED pins
#define RED_LED    16
#define YELLOW_LED 17
#define GREEN_LED  18

// Buzzer
#define BUZZER_PIN 19

// Known UIDs
const uint8_t uidYellow[] = { 0xD3, 0xDE, 0x28, 0xE1 };
const uint8_t uidGreen[]  = { 0xDF, 0x83, 0x08, 0x70 };
const uint8_t uidRed[]    = { 0xBC, 0xDE, 0x4F, 0x05 };

Adafruit_PN532 nfc(SDA_PIN, SCL_PIN);
LiquidCrystal_I2C lcd(0x27, 20, 4);

void setup() {
  Serial.begin(115200);
  delay(1000);

  Wire.begin(SDA_PIN, SCL_PIN);

  // LCD init
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Starting NFC...");

  // LED setup
  pinMode(RED_LED, OUTPUT);
  pinMode(YELLOW_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  turnOffAllLEDs();

  // Buzzer setup
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // PN532 init
  nfc.begin();
  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata) {
    lcd.setCursor(0, 1);
    lcd.print("PN532 not found");
    Serial.println("PN532 not found");
    while (1);
  }

  nfc.SAMConfig();
  lcd.setCursor(0, 1);
  lcd.print("PN532 Ready");
  lcd.setCursor(0, 2);
  lcd.print("Waiting for card");
}

void loop() {
  uint8_t uid[7]; // Max UID size
  uint8_t uidLength;

  // Look for card
  if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength)) {
    Serial.print("UID: ");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("UID: ");

    for (uint8_t i = 0; i < uidLength; i++) {
      Serial.print(uid[i], HEX);
      Serial.print(" ");
      lcd.print(uid[i] < 0x10 ? "0" : "");
      lcd.print(uid[i], HEX);
      lcd.print(" ");
    }
    Serial.println();

    lcd.setCursor(0, 2);

    // Compare to known UIDs
    if (uidLength == 4 && memcmp(uid, uidYellow, 4) == 0) {
      lcd.print("Yellow card found");
      showColor(YELLOW_LED);
      beepBuzzer(100);
      delay(100);
      beepBuzzer(100);
    } else if (uidLength == 4 && memcmp(uid, uidGreen, 4) == 0) {
      lcd.print("Green card found");
      showColor(GREEN_LED);
      beepBuzzer(100);
    } else if (uidLength == 4 && memcmp(uid, uidRed, 4) == 0) {
      lcd.print("Red card found");
      showColor(RED_LED);
      beepBuzzer(100);
      delay(100);
      beepBuzzer(100);
      delay(100);
      beepBuzzer(100);
    } else {
      lcd.print("Unknown card!");
      showAllColors();
      beepBuzzer(100);
      delay(100);
      beepBuzzer(100);
      delay(100);
      beepBuzzer(100);
    }

    delay(2000);
    lcd.setCursor(0, 3);
    lcd.print("Scan another card");
  }
}

void turnOffAllLEDs() {
  digitalWrite(RED_LED, LOW);
  digitalWrite(YELLOW_LED, LOW);
  digitalWrite(GREEN_LED, LOW);
}

void showColor(uint8_t pin) {
  turnOffAllLEDs();
  digitalWrite(pin, HIGH);
}

void showAllColors() {
  digitalWrite(RED_LED, HIGH);
  digitalWrite(YELLOW_LED, HIGH);
  digitalWrite(GREEN_LED, HIGH);
}

void beepBuzzer(uint8_t time) {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(time); // short beep
  digitalWrite(BUZZER_PIN, LOW);
}