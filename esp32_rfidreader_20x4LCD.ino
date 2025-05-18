#include <Wire.h>
#include <Adafruit_PN532.h>
#include <LiquidCrystal_I2C.h>

// I2C pins for ESP32 (same bus for PN532 and LCD)
#define SDA_PIN 21
#define SCL_PIN 22

// Initialize PN532 NFC reader (I2C)
Adafruit_PN532 nfc(SDA_PIN, SCL_PIN);

// Initialize 20x4 LCD at I2C address 0x27 (change if needed)
LiquidCrystal_I2C lcd(0x27, 20, 4);

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Initialize I2C bus for ESP32 (optional but recommended)
  Wire.begin(SDA_PIN, SCL_PIN);

  // Init LCD
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Starting NFC Reader");

  // Init PN532
  nfc.begin();
  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata) {
    Serial.println("Didn't find PN532 board :(");
    lcd.setCursor(0, 1);
    lcd.print("PN532 not found");
    while (1); // halt
  }

  Serial.print("Found PN532 with firmware version: ");
  Serial.print((versiondata >> 16) & 0xFF, HEX);
  Serial.print('.');
  Serial.println((versiondata >> 8) & 0xFF, HEX);

  lcd.setCursor(0, 1);
  lcd.print("PN532 found       ");
  lcd.setCursor(0, 2);
  lcd.print("Waiting for card...");

  nfc.SAMConfig();
}

void loop() {
  uint8_t success;
  uint8_t uid[7];    // Buffer to store returned UID
  uint8_t uidLength;

  // Wait for an NFC card (timeout 1 sec)
  success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength);

  if (success) {
    Serial.print("Found card with UID:");

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Card UID:");

    lcd.setCursor(0, 1);
    for (uint8_t i = 0; i < uidLength; i++) {
      Serial.print(uid[i] < 0x10 ? " 0" : " ");
      Serial.print(uid[i], HEX);

      // Display UID on LCD (space separated hex)
      if (i < 20) { // Prevent overflow on 20 chars width
        if (uid[i] < 0x10) lcd.print("0");
        lcd.print(uid[i], HEX);
        lcd.print(" ");
      }
    }
    Serial.println();

    lcd.setCursor(0, 2);
    lcd.print("Card detected!");

    delay(2000); // Show UID for 2 seconds
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Waiting for card...");
  }
}

/*
Device  Pin Name  ESP32 Pin Number  Notes
PN532 NFC SDA GPIO 21 I2C Data line (shared bus)
SCL GPIO 22 I2C Clock line (shared bus)
VCC 3.3V or 5V  Power (check your PN532 specs)
GND GND Ground
IRQ Not connected Optional, can be left unconnected
RST Not connected Optional, can be left unconnected

| 20x4 LCD (I2C) | SDA | GPIO 21 | I2C Data line (shared bus) |
| | SCL | GPIO 22 | I2C Clock line (shared bus) |
| | VCC | 5V (usually) | Power (5V typically required, check LCD specs) |
| | GND | GND | Ground |

*/