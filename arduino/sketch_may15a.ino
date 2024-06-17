#include <TM1637Display.h>
#include <SPI.h>
#include <MFRC522.h>
#include <EEPROM.h>
// #include <ESP8266WiFi.h>

//#define DEV_VER 

// Pins for the RC522
//#define RST_PIN 15
#define SS_PIN 15
#define MOSI_PIN 13
#define MISO_PIN 12
#define SCK_PIN 14

// Pins for other components
#ifdef DEV_VER
  #define DCIN_PIN 2              // DC power present = LOW, not present = HIGH
  #define BUTTON_PIN 4
  // Pins for the TM1637
  #define CLK 0
  #define DIO 16
#else
  #define DCIN_PIN 16              // DC power present = LOW, not present = HIGH
  #define BUTTON_PIN 3
  // Pins for the TM1637
  #define CLK 0
  #define DIO 1
#endif

#define COIL_PIN 5
#define VBAT_PIN A0
#define VBAT_DIVIDER 0.0051316  //depend on voltage divider, check schematic
#define VBAT_LOW_THRESHOLD 3.3

// Hardcoded NFC tag UIDs
byte K0[] = { 0x03, 0x11, 0xD4, 0x11 };  // Masterkey - K0 UID
byte K1[] = { 0x53, 0x17, 0xB3, 0x0D };  // K1 UID
byte K2[] = { 0xFE, 0xED, 0xFA, 0xCE };  // K2 UID

TM1637Display display(CLK, DIO);
MFRC522 mfrc522(SS_PIN, UINT8_MAX);
uint8_t bat_low[] = {
		 SEG_F | SEG_G | SEG_E | SEG_C | SEG_D,      // B
		 SEG_A | SEG_B | SEG_C | SEG_G | SEG_E | SEG_F,  // A
		 SEG_F | SEG_E | SEG_D,              // L
		 SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F,  // O
		};
uint8_t lock[] = {
		 SEG_F | SEG_E | SEG_D,              // L
		 SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F,  // O
		 SEG_A | SEG_D | SEG_E | SEG_F,  // C
		 SEG_G | SEG_E | SEG_F,  // K
		};
uint8_t unlock[] = {
		 SEG_B | SEG_C | SEG_D | SEG_E | SEG_F,  // U
		 SEG_A | SEG_B | SEG_C | SEG_E | SEG_F,  // N
		 SEG_F | SEG_E | SEG_D,              // L
		 SEG_G | SEG_E | SEG_F,  // K
		};

uint8_t deny[] = {
		 SEG_B | SEG_C | SEG_D | SEG_E | SEG_G,      // d
		 SEG_A | SEG_D | SEG_E | SEG_F | SEG_G,      // E
		 SEG_A | SEG_B | SEG_C | SEG_E | SEG_F,  // N
		 SEG_B | SEG_C | SEG_D | SEG_F | SEG_G,      // Y
		};
bool lastStateBatLow = false;
float vBat;
unsigned long vbatLowStartTime;
// unsigned long displayStartTime;
bool isLocked;

struct s_eppromStoring {
  bool isLock;
} eppromStoring;

void IRAM_ATTR handleButtonInterrupt() {
  if (!isLocked) {
  digitalWrite(COIL_PIN, HIGH);
  }
}
  
void setup() {
  #ifdef DEV_VER
  Serial.begin(115200);
  #endif

  EEPROM.begin(sizeof(s_eppromStoring));
  //SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, SS_PIN);
  SPI.begin();
  mfrc522.PCD_Init();

  display.setBrightness(0x0f);
  display.showNumberDec(8888, false);

  pinMode(DCIN_PIN, INPUT_PULLUP);
  pinMode(BUTTON_PIN, INPUT_PULLDOWN_16);
  pinMode(COIL_PIN, OUTPUT);
  
  EEPROM.get(0, eppromStoring);
  isLocked = eppromStoring.isLock;
  #ifdef DEV_VER
  Serial.println("\nRead lock state from flash:\n");
  Serial.print(isLocked);
  #endif
  
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), handleButtonInterrupt, RISING);
}

void loop() {

  // if DC power loss ---> check Vbat
  if (digitalRead(DCIN_PIN) == HIGH) {
    vBat = analogRead(VBAT_PIN) * VBAT_DIVIDER;
    // if Vbat is low for 10s --> set MCU into deepsleep
    if (vBat < VBAT_LOW_THRESHOLD) {
      // if Vbat change from high to low --> reset timer
      if (lastStateBatLow == false) {
        lastStateBatLow = true;

        #ifdef DEV_VER
        Serial.println("\nBattery low");
        #endif

        vbatLowStartTime = millis();
      }
      // if Vbat is low for 10s --> set MCU into deepsleep
      if (millis() - vbatLowStartTime > 10000) {
        #ifdef DEV_VER
        Serial.println("\nBattery low for 10s. I'm gonna shutdown. Bye :3");
        #endif
        display.clear();
        ESP.deepSleep(10000);
      }
    } else {
      lastStateBatLow = false;
    }
  }

  if (lastStateBatLow && (digitalRead(DCIN_PIN) == HIGH)) {
    display.setSegments(bat_low);
  } else {
    if (isLocked) {
      display.setSegments(lock);
    } else {
      display.setSegments(unlock);
    }
  }

  if (digitalRead(COIL_PIN) == LOW) {
    if (!mfrc522.PICC_IsNewCardPresent()) {
      return;
    }
    if (!mfrc522.PICC_ReadCardSerial()) {
      return;
    }

    byte *uid = mfrc522.uid.uidByte;
    byte uidSize = mfrc522.uid.size;

    if (compareUID(uid, uidSize, K0, sizeof(K0))) {
      display.showNumberDec(0);
      digitalWrite(COIL_PIN, HIGH);
      if (digitalRead(BUTTON_PIN) == HIGH) {
        if (isLocked) {
          isLocked = false;
          #ifdef DEV_VER
          Serial.println("\nUnlocked");
          #endif
          eppromStoring.isLock = isLocked;
          EEPROM.put(0, eppromStoring); //write data to array in ram 
          EEPROM.commit();  //write data from ram to flash memory. Do nothing if there are no changes to EEPROM data in ram
        } else {
          isLocked = true;
          #ifdef DEV_VER
          Serial.println("\nLocked");
          #endif
          eppromStoring.isLock = isLocked;
          EEPROM.put(0, eppromStoring); 
          EEPROM.commit();
        }
      }
    } else if (compareUID(uid, uidSize, K1, sizeof(K1))) {
      display.showNumberDec(1);
      digitalWrite(COIL_PIN, HIGH);
    } else if (compareUID(uid, uidSize, K2, sizeof(K2))) {
      display.showNumberDec(2);
      digitalWrite(COIL_PIN, HIGH);
      } else {
        if (!isLocked) {
          digitalWrite(COIL_PIN, HIGH);
        } else {
        display.setSegments(deny);
        delay(500);
        }
      }
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
  }

  if (digitalRead(COIL_PIN) == HIGH) {  
    delay(3000);
    display.clear();
    digitalWrite(COIL_PIN, LOW);
  }
}

int compareUID(byte *uid1, byte uidSize1, byte *uid2, byte uidSize2) {
  if (uidSize1 != uidSize2) {
    return false;
  }
  for (byte i = 0; i < uidSize1; i++) {
    if (uid1[i] != uid2[i]) {
      return false;
    }
  }
  return true;
}
