/*
DMD Page Loader
Author: Kevin Ahr
*/

#include <DMD2.h>
#include <SPI.h>
#include <SdFat.h>

#include <Buzzer.h>
#include <EEPROMex.h>
#include <Encoder.h>
#include <OneButton.h>
#include <MemoryFree.h>
#include <ArduinoQueue.h>

#include "Arial_Black_16.h"
#include "Droid_Sans_12.h"
#include "NumberFont3x5.h"
#include "SystemFont5x7.h"

// SD_FAT_TYPE = 0 for SdFat/File as defined in SdFatConfig.h,
// 1 for FAT16/FAT32, 2 for exFAT, 3 for FAT16/FAT32 and exFAT.
#define SD_FAT_TYPE 3

// Maximum file name length (with extention)
#define MAX_FILE_LEN 15

#define EEPROM_MAX_WRITES 80
#define EEPROM_BASE 350

#define BUZZER_PIN 22

#define ENC_BTN 4
#define ENC_A 3
#define ENC_B 2

#define FX_BAUD 115200
#define FX_RX_Q 10
#define FX_MAX_CMD 32
#define FX_LINE_ENDING '\n'
#define FX_LINE_ENDING_STR "\n"

// SDCARD_SS_PIN is defined for the built-in SD on some boards.
#ifndef SDCARD_SS_PIN
const uint8_t SD_CS_PIN = SS;
#else  // SDCARD_SS_PIN
// Assume built-in SD is used.
const uint8_t SD_CS_PIN = SDCARD_SS_PIN;
#endif // SDCARD_SS_PIN

// Try max SPI clock for an SD. Reduce SPI_CLOCK if errors occur.
#define SPI_CLOCK SD_SCK_MHZ(200)

// Try to select the best SD card configuration.
#if HAS_SDIO_CLASS
#define SD_CONFIG SdioConfig(FIFO_SDIO)
#elif ENABLE_DEDICATED_SPI
#define SD_CONFIG SdSpiConfig(SD_CS_PIN, SPI_CLOCK)
#else // HAS_SDIO_CLASS
#define SD_CONFIG SdSpiConfig(SD_CS_PIN, SHARED_SPI, SPI_CLOCK)
#endif // HAS_SDIO_CLASS

#if SD_FAT_TYPE == 0
SdFat sd;
File root;
File file;
#elif SD_FAT_TYPE == 1
SdFat sd;
FatFile root;
FatFile file;
#elif SD_FAT_TYPE == 2
SdExFat sd;
ExFile root;
ExFile file;
#elif SD_FAT_TYPE == 3
SdFs sd;
FsFile root;
FsFile file;
#else // SD_FAT_TYPE
#error invalid SD_FAT_TYPE
#endif // SD_FAT_TYPE

#define VERSION "V100"

SoftDMD dmd(1, 2); // DMD controls the entire display(s)
OneButton btn(ENC_BTN);
Encoder enc(ENC_A, ENC_B);
ArduinoQueue<char*> fxQueue(FX_RX_Q);

DMDFrame frame = DMDFrame(dmd.width, dmd.height);
uint8_t fileBuffer[1025];
char fileName[MAX_FILE_LEN];
unsigned int files = 0;

unsigned long previousMillis = 0;
unsigned long btnPress = 0;
unsigned long btnRelease = 0;

int timebarPos = 1;
int pageTime = 1000;
int pageTimeMult = 1;

bool settingsLoaded = 0;
bool paused = 0;

int brightness = 127;

int settingsSelectedItem = 0;
int settingsActiveItem = -1;
bool settingsScroll = true;
int settingsCurrentValue;

char *fx = (char*)malloc(FX_MAX_CMD);

void wipeAni();

void loadSettings();

void loadPic(const uint8_t *pic);

void delayBar(unsigned int time);

void dispError(uint8_t code);

void dispLoad(const String &pcnt);

void backgroundUpdate();

void onClick();

void onLong();

void addSettingsItems();

void drawArrow(int pos);

int EndsWith(const char *str, const char *suffix);

int euclidean_modulo(int a, int b);

bool inRange(int val, int minimum, int maximum);

void setup() {
  Serial.begin(FX_BAUD);
  sd.initErrorPrint(&Serial);

  dmd.begin();
  dmd.setBrightness(brightness);
  dmd.selectFont(Arial_Black_16);

  pinMode(BUZZER_PIN, OUTPUT);

  btn.attachClick(onClick);
  btn.attachLongPressStart(onLong);

  loadSettings();

  dispLoad(F("33"));

  // Initialize the SD.
  if (!sd.begin(SD_CONFIG)) {
    // dispError(1);
    // while (true) {
    //   buzzer.sound(NOTE_C2, 100);
    //   buzzer.sound(0, 50);
    //   buzzer.sound(NOTE_C2, 100);
    //   buzzer.sound(0, 150);
    // }
    // not sure why it returns false, yet success
  }

  dispLoad(F("67"));

  // Open root directory
  if (!root.open("/")) {
    // dispError(2);
    Serial.println("Fail2");
    // while (true) {
    //   buzzer.sound(NOTE_C2, 100);
    //   buzzer.sound(0, 50);
    //   buzzer.sound(NOTE_C2, 100);
    //   buzzer.sound(0, 150);
    // }
  }
  // if (root.getError()) {
  //   dispError(3);
  //   Serial.println("Fail3");
  //   while (true) {
  //     buzzer.sound(NOTE_C2, 100);
  //     buzzer.sound(0, 50);
  //     buzzer.sound(NOTE_C2, 100);
  //     buzzer.sound(0, 150);
  //   }
  // }

  dispLoad(F("100"));

  // wipeAni();
}

void loop() {
  if (Serial.available()) {
    int bytesRead = Serial.readBytesUntil(FX_LINE_ENDING, fx, FX_MAX_CMD - 1);
    fx[bytesRead] = '\0';
    fxQueue.enqueue(fx);
    return;
  }

  Serial.print("memfree=");
  Serial.println(freeMemory());

  Serial.print("uptime=");
  Serial.println(millis());

  Serial.print("maxq=");
  Serial.println(fxQueue.maxQueueSize());

  Serial.print("qitems=");
  Serial.println(fxQueue.itemCount());

  if (!fxQueue.isEmpty()) {
    char *fxRaw = (char*)malloc(FX_MAX_CMD);
    fxRaw = fxQueue.dequeue();
    // parse command=value
    char *fxCmd = strtok(fxRaw, "=");
    char *fxVal = strtok(NULL, FX_LINE_ENDING_STR);
    Serial.print("fxCmd=");
    Serial.println(fxCmd);
    Serial.print("fxVal=");
    Serial.println(fxVal);

  }
  
    if (settingsLoaded == 0) {
      // Attempt to open the next file
      int rc = file.openNext(&root, FILE_READ);
      if (!rc) {
          Serial.println("state=rewind");
          root.rewindDirectory();  // Reset directory reading position
          return;
      }
    
      // Print file name
      file.getName(fileName, sizeof(fileName));

      Serial.print("file=");
      Serial.println(fileName);
    
      Serial.println("state=animate");
      
      if (EndsWith(fileName, ".DMD")) {
        if (true) {
          file.readBytes(fileBuffer, 1025);
          loadPic(fileBuffer);
          if (pageTime > 0) {
            pageTimeMult = fileBuffer[0];
            delayBar(pageTime / 32 * pageTimeMult);
          } else {
            backgroundUpdate();
          }
        } else {
          dispError(4);
          while (true) {
          }
        }
      } else {
        backgroundUpdate();
      }
    } else {
      Serial.println("state=settings");
      backgroundUpdate();
    }
    file.close();
}

void wipeAni() {
  for (int i = 0; i < 32; i++) {
    dmd.drawLine(0, i, 31, i);
    delay(10);
  }
}

void loadSettings() {
  EEPROM.setMemPool(EEPROM_BASE, EEPROMSizeMega);
  EEPROM.setMaxAllowedWrites(EEPROM_MAX_WRITES);
  if (inRange(EEPROM.readInt(EEPROM_BASE), 0, 9990)) {
    pageTime = EEPROM.readInt(EEPROM_BASE);
  }

  if (inRange(EEPROM.readByte(EEPROM_BASE + 2), 0, 2)) {
    timebarPos = EEPROM.readByte(EEPROM_BASE + 2);
  }

  if (inRange(EEPROM.readByte(EEPROM_BASE + 3), 1, 255)) {
    brightness = EEPROM.readByte(EEPROM_BASE + 3);
    dmd.setBrightness(brightness);
  }
}

void loadPic(const uint8_t *pic) {
  const uint8_t *p = pic + 1;  // Start from index 1

  for (unsigned int y = 0; y < 32; y++) {
    for (unsigned int x = 0; x < 32; x++) {
      frame.setPixel(x, y, (*p++) ? GRAPHICS_ON : GRAPHICS_OFF);
    }
  }

  dmd.copyFrame(frame, 0, 0);
}


void delayBar(unsigned int time) {
  // tone(BUZZER_PIN, 440);
  for (int i = 0; i < 32; i++) {
    if (!paused and !settingsLoaded) {
      if (timebarPos == 1) {
        dmd.setPixel(i, 31, GRAPHICS_XOR);
      } else if (timebarPos == 2) {
        dmd.setPixel(i, 0, GRAPHICS_XOR);
      }
    }
    while (1) {
      backgroundUpdate();
      unsigned long currentMillis = millis();

      if (paused or settingsLoaded) {
        break;
      }

      if (currentMillis - previousMillis >= time) {
        previousMillis = currentMillis;
        break;
      }
    }
  }
  // noTone(BUZZER_PIN);
}

void dispError(uint8_t code) {
  dmd.selectFont(Arial_Black_16);

  char codeBuf[2];

  dmd.clearScreen();
  dmd.drawString(0, 1, "ERR");
  itoa(code, codeBuf, 10);
  dmd.drawString(0, 17, codeBuf);
}

void dispLoad(const String &pcnt) {
  dmd.selectFont(Droid_Sans_12);
  dmd.clearScreen();
  dmd.drawString(0, 1, "LOAD");
  dmd.drawString(0, 17, pcnt);
}

void backgroundUpdate() {

  btn.tick();

  if (settingsLoaded) {
    if (settingsScroll) {
      if (settingsSelectedItem != euclidean_modulo(enc.read() / 4, 3)) {
        settingsSelectedItem = euclidean_modulo(enc.read() / 4, 3);
        dmd.clearScreen();
        addSettingsItems();
        drawArrow(settingsSelectedItem);
      }
    } else if (settingsActiveItem == 0) {
      if (settingsCurrentValue != euclidean_modulo(enc.read() / 4, 1000)) {
        settingsCurrentValue = euclidean_modulo(enc.read() / 4, 1000);
        pageTime = settingsCurrentValue * 10;
        dmd.clearScreen();
        addSettingsItems();
        drawArrow(settingsSelectedItem);
      }
    } else if (settingsActiveItem == 1) {
      if (settingsCurrentValue != euclidean_modulo(enc.read() / 4, 3)) {
        settingsCurrentValue = euclidean_modulo(enc.read() / 4, 3);
        timebarPos = settingsCurrentValue;
        dmd.clearScreen();
        addSettingsItems();
        drawArrow(settingsSelectedItem);
      }
    } else if (settingsActiveItem == 2) {
      if (settingsCurrentValue != euclidean_modulo(enc.read() / 4, 255)) {
        settingsCurrentValue = euclidean_modulo(enc.read() / 4, 255);
        brightness = settingsCurrentValue + 1;
        dmd.setBrightness(brightness);

        dmd.clearScreen();
        addSettingsItems();
        drawArrow(settingsSelectedItem);
      }
    }
  }
}

void onClick() {
  tone(BUZZER_PIN, NOTE_B3, 10);
  if (!settingsLoaded) {
    paused = !paused;
  } else {
    if (settingsActiveItem == settingsSelectedItem) {
      enc.write(settingsSelectedItem * 4);
      settingsActiveItem = -1;
      settingsScroll = true;
    } else {
      settingsActiveItem = settingsSelectedItem;
      settingsScroll = false;

      if (settingsActiveItem == 0) {
        enc.write(pageTime / 10 * 4);
      } else if (settingsActiveItem == 1) {
        enc.write(timebarPos * 4);
      } else if (settingsActiveItem == 2) {
        enc.write((brightness - 1) * 4);
      }
    }
    dmd.clearScreen();
    addSettingsItems();
    drawArrow(settingsSelectedItem);
  }
}

void onLong() {
  if (!paused) {
    settingsLoaded = !settingsLoaded;
    dmd.clearScreen();
    tone(BUZZER_PIN, NOTE_C4, 10);
  }

  if (settingsLoaded) {
    addSettingsItems();
    drawArrow(settingsSelectedItem);
  } else {
    settingsActiveItem = -1;
    settingsSelectedItem = 0;
    settingsScroll = true;

    // Save settings
    EEPROM.writeInt(EEPROM_BASE, pageTime);
    EEPROM.writeByte(EEPROM_BASE + 2, timebarPos);
    EEPROM.writeByte(EEPROM_BASE + 3, brightness);
  }
}

void addSettingsItems() {
  dmd.selectFont(SystemFont5x7);
  dmd.drawString(0, 0, "SP");
  dmd.drawString(0, 8, "TB");
  dmd.drawString(0, 16, "BR");
  dmd.drawString(0, 24, VERSION);

  dmd.selectFont(NumberFont3x5);
  dmd.drawString(14, 1, String(pageTime / 10));
  dmd.drawString(14, 9, String(timebarPos));
  dmd.drawString(14, 17, String(brightness));
}

void drawArrow(int pos) {
  dmd.selectFont(SystemFont5x7);
  if (pos == settingsActiveItem) {
    dmd.drawString(27, pos * 8, "<");
  } else {
    dmd.drawString(27, pos * 8, "(");
  }
}

int EndsWith(const char *str, const char *suffix) {
  if (!str || !suffix)
    return 0;
  size_t lenstr = strlen(str);
  size_t lensuffix = strlen(suffix);
  if (lensuffix > lenstr)
    return 0;
  return strncasecmp(str + lenstr - lensuffix, suffix, lensuffix) == 0;
}

int euclidean_modulo(int a, int b) {
  int m = a % b;
  if (m < 0) {
    // m += (b < 0) ? -b : b; // avoid this form: it is UB when b == INT_MIN
    m = (b < 0) ? m - b : m + b;
  }
  return m;
}

bool inRange(int val, int minimum, int maximum) {
  return ((minimum <= val) && (val <= maximum));
}